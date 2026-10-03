// yacoin-cpuminer: entry point. MIT licence.
#include <cstdio>
#include <iostream>

#include "app/options.h"
#include "hash/pow_hash.h"
#include "rpc/node_api.h"
#include "util/hex.h"
#include "util/log.h"
#include "work/target.h"

using namespace yac;

// --check-work: one getwork fetch, decoded and checked. Never submits.
static int check_work(const Options& o)
{
    RpcNodeApi api(o.rpc);
    MiningInfo mi = api.mining_info();
    std::string tip = api.best_block_hash();
    log_info("node: height " + std::to_string(mi.blocks) + ", Nfactor " + std::to_string(mi.nfactor) + ", tip " + tip);
    Work w = api.get_work();
    const BlockHeader& h = w.header;
    char bits[16];
    std::snprintf(bits, sizeof bits, "%08x", h.bits);
    log_info("getwork: version " + std::to_string(h.version) + ", prev " + h.prev_hex() + ", merkle " +
             h.merkle_hex() + ", time " + std::to_string(h.time) + ", bits " + bits + ", nonce " +
             std::to_string(h.nonce));
    log_info("target " + to_hex_reversed(w.target.data(), 32) + " (~" +
             fmt_double(expected_hashes(w.target), 0) + " hashes per block)");
    bool ok = true;
    auto check = [&](bool c, const std::string& what) {
        log_info(std::string(c ? "  ok    " : "  FAIL  ") + what);
        ok = ok && c;
    };
    check(h.version >= 7, "version >= 7");
    check(h.prev_hex() == tip, "prev_block == node tip");
    check(w.target == target_from_compact(h.bits), "target == nBits expanded");
    check(mi.nfactor == o.nfactor, "node Nfactor == --nfactor (" + std::to_string(o.nfactor) + ")");
    return ok ? 0 : 1;
}

int main(int argc, char** argv)
{
    Options o;
    try {
        o = parse_options(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "yacoin-cpuminer: " << e.what() << "\n";
        return 2;
    }
    if (o.help) {
        std::cout << usage();
        return 0;
    }
    if (!scrypt_self_test()) {
        log_error("scrypt-jane self-test failed");
        return 1;
    }
    if (o.version) {
        std::cout << "yacoin-cpuminer " << YAC_MINER_VERSION << " (" << scrypt_variant() << ")\n";
        return 0;
    }
    for (const std::string& f : {o.conf, o.yacoin_conf}) {
        if (f.empty()) continue;
        std::string w = permission_warning(f);
        if (!w.empty()) log_warn(w);
    }
    rpc_global_init();
    try {
        if (o.check_work) return check_work(o);
        log_error("mining is not implemented yet (T-04)");
        return 1;
    } catch (const std::exception& e) {
        log_error(e.what());
        return 1;
    }
}
