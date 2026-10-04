// yacoin-cpuminer: entry point. MIT licence.
#include <sys/resource.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <iostream>

#include "app/options.h"
#include "hash/pow_hash.h"
#include "miner/miner.h"
#include "rpc/node_api.h"
#include "util/hex.h"
#include "util/log.h"
#include "util/sysinfo.h"
#include "work/target.h"

using namespace yac;

static std::atomic<bool> g_stop{false};

// First SIGINT/SIGTERM: clean stop (final stats). A second one exits at once,
// e.g. while a long RPC call is still waiting for its timeout.
extern "C" void on_signal(int)
{
    if (g_stop.exchange(true)) _exit(130);
}

static void install_signal_handlers()
{
    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    // If stdout is a pipe (e.g. `| tee`) that goes away, keep running and
    // still stop cleanly instead of dying from SIGPIPE.
    signal(SIGPIPE, SIG_IGN);
}

// Applies the nice level to this thread before workers start; Linux threads
// created afterwards inherit it.
static void apply_nice(int nice)
{
    if (nice <= 0) return;
    if (setpriority(PRIO_PROCESS, 0, nice) != 0) log_warn("could not set nice level " + std::to_string(nice));
}

static std::string gib(double bytes)
{
    if (bytes < 1024.0 * 1024 * 1024) return fmt_double(bytes / (1024.0 * 1024), 2) + " MiB";
    return fmt_double(bytes / (1024.0 * 1024 * 1024), 2) + " GiB";
}

// threads x scratch must fit in available memory, with headroom for the
// node's own 512 MiB hash on every submit (plan §5 Memory).
// threads x lanes tables, plus one more for the reference re-hash before a
// submit (plan §12).
static bool memory_ok(const Options& o, bool mining)
{
    double scratch = double(scratch_bytes(o.nfactor));
    double need = double(o.threads) * o.lanes * scratch + (mining ? scratch : 0);
    double headroom = o.nfactor >= 20 ? 1024.0 * 1024 * 1024 : 64.0 * 1024 * 1024;
    double avail = double(mem_available_bytes());
    log_info("memory: " + std::to_string(o.threads) + " threads x " + std::to_string(o.lanes) + " lanes x " +
             gib(scratch) + (mining ? " + 1 for the submit check" : "") + " = " + gib(need) + "; available " +
             (avail > 0 ? gib(avail) : std::string("unknown")));
    if (avail > 0 && need + headroom > avail) {
        if (o.ignore_memory_check) {
            log_warn("not enough free memory (need " + gib(need + headroom) + " incl. headroom); continuing (--ignore-memory-check)");
            return true;
        }
        log_error("not enough free memory: need " + gib(need) + " + " + gib(headroom) + " headroom, available " +
                  gib(avail) + ". Use fewer --threads or --lanes (or --ignore-memory-check).");
        return false;
    }
    return true;
}

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

// Fused mixes need an AVX2 build and at least 2 lanes; refuse a setting
// that would silently run the plain mix (and mislabel a benchmark).
static bool lane_options_ok(const Options& o)
{
    if (o.mix != "plain" && !fused_mix_available()) {
        log_error("--mix " + o.mix + " needs a build with AVX2 (-march=native on an AVX2 CPU); this build has none");
        return false;
    }
    if (o.mix != "plain" && o.lanes < 2) {
        log_error("--mix " + o.mix + " needs --lanes 2 or more");
        return false;
    }
    return true;
}

static unsigned lane_flags(const Options& o)
{
    unsigned f = o.prefetch == "nta" ? kPrefetchNta : o.prefetch == "none" ? kPrefetchNone : kPrefetchT0;
    if (o.mix == "fused2") f |= kMixFused2;
    if (o.mix == "fused4") f |= kMixFused4;
    return f;
}

static int benchmark(const Options& o)
{
    log_info("benchmark: " + std::to_string(o.threads) + " threads x " + std::to_string(o.lanes) + " lanes (prefetch " +
             o.prefetch + ", mix " + o.mix + "), N-factor " + std::to_string(o.nfactor) + ", " +
             fmt_double(o.bench_seconds, 0) + " s (first " + fmt_double(o.bench_warmup, 0) + " s not counted), " + scrypt_variant() + ", huge pages " +
             (o.huge_pages ? "requested" : "off") + ", nice " + std::to_string(o.nice));
    if (!lane_options_ok(o) || !memory_ok(o, false)) return 1;
    std::vector<double> per;
    double total = run_benchmark(static_cast<unsigned>(o.threads), o.nfactor, o.bench_seconds, o.huge_pages, per, g_stop, o.bench_warmup,
                                 static_cast<unsigned>(o.lanes), lane_flags(o));
    std::string s;
    for (size_t i = 0; i < per.size(); ++i) s += (i ? " " : "") + fmt_double(per[i], 3);
    log_info("benchmark result: total " + fmt_double(total, 3) + " H/s, per thread [" + s + "]");
    if (o.nfactor == kMainnetNFactor && total > 0) {
        double hours = expected_hashes(target_from_compact(0x1e0fffff)) / total / 3600.0;
        log_info("at mainnet minimum difficulty (1e0fffff): expected " + fmt_double(hours, 1) + " h per block");
    }
    std::printf("BENCH threads=%d lanes=%d mix=%s prefetch=%s nfactor=%u hugepages=%d total_hps=%.4f\n", o.threads,
                o.lanes, o.mix.c_str(), o.prefetch.c_str(), o.nfactor, o.huge_pages ? 1 : 0, total);
    return 0;
}

static int mine(const Options& o)
{
    log_info("yacoin-cpuminer " + std::string(YAC_MINER_VERSION) + " (" + scrypt_variant() + "), node " +
             o.rpc.describe() + ", " + std::to_string(o.threads) + " threads x " + std::to_string(o.lanes) +
             " lanes (mix " + o.mix + ", prefetch " + o.prefetch + "), N-factor " + std::to_string(o.nfactor) +
             ", nice " + std::to_string(o.nice) + ", huge pages " + (o.huge_pages ? "on" : "off"));
    if (o.rpc.user.empty() || o.rpc.password.empty())
        log_warn("no rpcuser/rpcpassword configured (config file " + o.conf + ")");

    // Start-up checks (plan §5, T-04 step 4).
    RpcNodeApi api(o.rpc);
    MiningInfo mi = api.mining_info();
    if (mi.nfactor != o.nfactor) {
        log_error("the node reports Nfactor " + std::to_string(mi.nfactor) + " but --nfactor is " +
                  std::to_string(o.nfactor) + "; refusing to mine (hashes would never match)");
        return 1;
    }
    NodeInfo ni = api.node_info();
    log_info("node " + ni.version + ": height " + std::to_string(ni.blocks) + ", connections " +
             std::to_string(ni.connections) + ", keypool " + std::to_string(ni.keypoolsize) +
             (ni.unlocked_until ? ", wallet encrypted (unlocked_until " + std::to_string(*ni.unlocked_until) + ")"
                                : ", wallet not encrypted"));
    bool locked = ni.unlocked_until && *ni.unlocked_until == 0;
    if (locked) {
        log_error("the node's wallet is locked: it cannot sign found blocks (getwork error -100) and an empty "
                  "keypool could crash it (plan F12). Unlock it first.");
        return 1;
    }
    if (ni.unlocked_until && ni.keypoolsize == 0) {
        log_error("the node's wallet is encrypted and its keypool is empty: if the wallet locks, getwork could "
                  "crash the node (plan F12). Run keypoolrefill on the node first.");
        return 1;
    }
    if (ni.keypoolsize == 0) log_warn("the node's keypool is empty; run keypoolrefill on the node");
    if (ni.unlocked_until && *ni.unlocked_until > 0)
        log_warn("the node's wallet is unlocked only until " + std::to_string(*ni.unlocked_until) +
                 "; once it locks, found blocks cannot be signed. The miner stops calling getwork if the keypool "
                 "then runs empty.");
    if (ni.connections == 0) log_warn("the node has no peers: getwork is refused until it has one (plan F8)");
    if (!lane_options_ok(o) || !memory_ok(o, true)) return 1;

    MinerConfig cfg;
    cfg.threads = static_cast<unsigned>(o.threads);
    cfg.lanes = static_cast<unsigned>(o.lanes);
    cfg.lane_flags = lane_flags(o);
    cfg.nfactor = o.nfactor;
    cfg.huge_pages = o.huge_pages;
    cfg.tip_poll_s = o.tip_poll_s;
    cfg.work_refresh_s = o.work_refresh_s;
    cfg.retry_s = o.retry_s;
    cfg.stats_s = o.stats_s;
    cfg.max_blocks = o.max_blocks;
    RpcSettings rpc = o.rpc;
    Miner m(cfg, [rpc] { return std::make_unique<RpcNodeApi>(rpc); }, g_stop);
    int rc = m.run();
    log_info("stopped");
    return rc;
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
    // Before any thread hashes (plan F4). Exits with code 21 on failure.
    scrypt_self_test();
    if (o.version) {
        std::cout << "yacoin-cpuminer " << YAC_MINER_VERSION << " (" << scrypt_variant() << ")\n";
        return 0;
    }
    for (const std::string& f : {o.conf, o.yacoin_conf}) {
        if (f.empty()) continue;
        std::string w = permission_warning(f);
        if (!w.empty()) log_warn(w);
    }
    install_signal_handlers();
    apply_nice(o.nice);
    rpc_global_init();
    try {
        if (o.benchmark) return benchmark(o);
        if (o.check_work) return check_work(o);
        return mine(o);
    } catch (const std::exception& e) {
        log_error(e.what());
        return 1;
    }
}
