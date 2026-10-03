// yacoin-cpuminer: the node calls the miner needs (plan F6-F9, F12). MIT licence.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "rpc/rpc_client.h"
#include "work/getwork.h"

namespace yac {

constexpr int kRpcInWarmup = -28;

struct MiningInfo {
    int64_t blocks = 0;
    unsigned nfactor = 0;
    uint64_t n = 0;
};

struct NodeInfo {
    std::string version;
    int64_t blocks = 0;
    int connections = 0;
    int64_t keypoolsize = -1;
    // Present only for an encrypted wallet: 0 = locked, else unlock expiry.
    std::optional<int64_t> unlocked_until;
};

// Interface so that the mining loop can be tested without a node.
class NodeApi {
public:
    virtual ~NodeApi() = default;
    virtual std::string best_block_hash() = 0;  // cheap tip poll
    virtual Work get_work() = 0;                 // getwork (no argument): not read-only (F12)
    virtual bool submit_work(const std::string& data_hex) = 0;  // getwork <data>
    virtual MiningInfo mining_info() = 0;
    virtual NodeInfo node_info() = 0;
};

// NodeApi over JSON-RPC. One instance per thread (RpcClient is not thread-safe).
class RpcNodeApi : public NodeApi {
public:
    explicit RpcNodeApi(const RpcSettings& s) : rpc_(s) {}
    std::string best_block_hash() override;
    Work get_work() override;
    bool submit_work(const std::string& data_hex) override;
    MiningInfo mining_info() override;
    NodeInfo node_info() override;

private:
    RpcClient rpc_;
};

// How the miner should react to an error from a getwork fetch or submit.
enum class RpcFailure {
    Transient,     // no peers, initial download, warm-up, node unreachable: retry later (F8)
    WalletLocked,  // -100 on submit: retry, but tell the user loudly
    Auth,          // wrong credentials: stop
    Other,         // anything else: log, and treat as a failed attempt
};
RpcFailure classify_rpc_failure(const std::exception& e);

}  // namespace yac
