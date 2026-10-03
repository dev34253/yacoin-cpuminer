// yacoin-cpuminer: an in-process stand-in for yacoind's getwork (tests only). MIT licence.
//
// It follows the node's logic (yacoin src/rpc/mining.cpp getwork,
// src/miner.cpp FormatHashBuffers_64bit_nTime and CheckWork): work is saved by
// merkle root; a submit takes only time and nonce from the data, recomputes the
// PoW hash, and checks target and tip. Thread-safe.
#pragma once

#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hash/pow_hash.h"
#include "rpc/node_api.h"
#include "util/hex.h"
#include "work/getwork.h"
#include "work/target.h"

namespace yac_test {

// The node's FormatHashBuffers_64bit_nTime: SHA-256 padding + word reversal.
inline std::string node_format_data(const uint8_t* header84)
{
    uint8_t buf[128] = {};
    std::memcpy(buf, header84, 84);
    buf[84] = 0x80;
    unsigned bits = 84 * 8;
    buf[127] = bits & 0xff;
    buf[126] = (bits >> 8) & 0xff;
    yac::reverse_words(buf, sizeof buf);
    return yac::to_hex(buf, sizeof buf);
}

class NodeSim {
public:
    NodeSim(unsigned nfactor, uint32_t bits) : nfactor_(nfactor), bits_(bits)
    {
        tip_.fill(0);
        tip_[0] = 1;
    }

    // ---- knobs
    void set_peers(bool on) { std::lock_guard<std::mutex> l(mu_); peers_ = on; }
    void refuse_next_submits(int n) { std::lock_guard<std::mutex> l(mu_); refuse_submits_ = n; }
    // The next submits fail like an unreachable node (connection refused).
    void unreachable_next_submits(int n) { std::lock_guard<std::mutex> l(mu_); unreachable_submits_ = n; }
    // A node restart: saved blocks are lost, the tip stays (F9, F11 "No saved block").
    void restart() { std::lock_guard<std::mutex> l(mu_); saved_.clear(); }
    // Restart right after the next getwork, so its work is unknown on submit.
    void restart_after_next_getwork() { std::lock_guard<std::mutex> l(mu_); restart_after_getwork_ = true; }
    // Another miner finds a block: the tip moves, saved blocks are cleared.
    void external_block()
    {
        std::lock_guard<std::mutex> l(mu_);
        tip_[1]++;
        tip_[2] = 0xee;
        ++height_;
        saved_.clear();
    }

    // ---- inspection
    std::string tip_hex() { std::lock_guard<std::mutex> l(mu_); return yac::to_hex_reversed(tip_.data(), 32); }
    int height() { std::lock_guard<std::mutex> l(mu_); return height_; }
    int getwork_calls() { std::lock_guard<std::mutex> l(mu_); return getwork_calls_; }
    int submit_calls() { std::lock_guard<std::mutex> l(mu_); return submit_calls_; }
    std::vector<std::string> accepted() { std::lock_guard<std::mutex> l(mu_); return accepted_; }

    // ---- the RPCs
    std::string best_block_hash() { return tip_hex(); }

    yac::Work get_work()
    {
        std::lock_guard<std::mutex> l(mu_);
        ++getwork_calls_;
        if (!peers_) throw yac::RpcError(yac::kRpcClientNotConnected, "getwork: Yacoin is not connected! (code -9)");
        yac::BlockHeader h;
        h.version = 7;
        h.prev_block = tip_;
        h.merkle_root.fill(0);
        uint64_t extra = ++extra_nonce_;
        std::memcpy(h.merkle_root.data(), &extra, sizeof extra);  // new merkle root per fetch (F9)
        h.time = 1791000000 + height_ * 60 + static_cast<int64_t>(extra);
        h.bits = bits_;
        h.nonce = 0;
        saved_[h.merkle_root] = h;
        if (restart_after_getwork_) {
            restart_after_getwork_ = false;
            saved_.clear();
        }
        auto bytes = h.serialize();
        yac::Hash256 t = yac::target_from_compact(bits_);
        return yac::decode_getwork(node_format_data(bytes.data()), yac::to_hex(t.data(), 32));
    }

    bool submit_work(const std::string& data_hex)
    {
        std::lock_guard<std::mutex> l(mu_);
        ++submit_calls_;
        if (!peers_) throw yac::RpcError(yac::kRpcClientNotConnected, "getwork: Yacoin is not connected! (code -9)");
        if (unreachable_submits_ > 0) {
            --unreachable_submits_;
            throw yac::RpcTransportError("getwork: Couldn't connect to server (simulated)");
        }
        if (refuse_submits_ > 0) {
            --refuse_submits_;
            throw yac::RpcError(yac::kRpcClientNotConnected, "getwork: Yacoin is not connected! (code -9)");
        }
        auto data = yac::from_hex(data_hex);
        if (data.size() != 128) throw yac::RpcError(-8, "Invalid parameter");
        yac::reverse_words(data.data(), data.size());
        yac::BlockHeader sub = yac::BlockHeader::parse(data.data());
        auto it = saved_.find(sub.merkle_root);
        if (it == saved_.end()) return false;  // "No saved block"
        yac::BlockHeader b = it->second;       // version, prev, bits from the saved block
        b.time = sub.time;                     // only time and nonce from the data (F7)
        b.nonce = sub.nonce;
        auto bytes = b.serialize();
        yac::Hash256 hash = yac::pow_hash_reference(bytes.data(), bytes.size(), nfactor_);
        if (!yac::hash_meets_target(hash, yac::target_from_compact(b.bits))) return false;
        if (b.prev_block != tip_) return false;  // stale
        tip_ = hash;
        ++height_;
        saved_.clear();
        accepted_.push_back(yac::to_hex_reversed(hash.data(), 32));
        return true;
    }

private:
    std::mutex mu_;
    unsigned nfactor_;
    uint32_t bits_;
    yac::Hash256 tip_{};
    int height_ = 0;
    bool peers_ = true;
    int refuse_submits_ = 0;
    int unreachable_submits_ = 0;
    bool restart_after_getwork_ = false;
    uint64_t extra_nonce_ = 0;
    std::map<yac::Hash256, yac::BlockHeader> saved_;
    int getwork_calls_ = 0;
    int submit_calls_ = 0;
    std::vector<std::string> accepted_;
};

// NodeApi view of a shared NodeSim (one per miner thread, like RpcNodeApi).
class SimApi : public yac::NodeApi {
public:
    explicit SimApi(std::shared_ptr<NodeSim> sim) : sim_(std::move(sim)) {}
    std::string best_block_hash() override { return sim_->best_block_hash(); }
    yac::Work get_work() override { return sim_->get_work(); }
    bool submit_work(const std::string& d) override { return sim_->submit_work(d); }
    yac::MiningInfo mining_info() override { return {}; }
    yac::NodeInfo node_info() override { return {}; }

private:
    std::shared_ptr<NodeSim> sim_;
};

}  // namespace yac_test
