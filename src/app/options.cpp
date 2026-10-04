// yacoin-cpuminer: command-line and config-file options. MIT licence.
#include "app/options.h"

#include <sys/stat.h>

#include <map>
#include <stdexcept>
#include <vector>

namespace yac {

std::string usage()
{
    return R"(Usage: yacoin-cpuminer [options]

Mines Yacoin proof-of-work blocks on the CPU via the node's getwork RPC.

Connection (settings: defaults < config files < command line):
  --conf FILE           miner config (default $XDG_CONFIG_HOME or ~/.config,
                        then yacoin-cpuminer/miner.conf):
                        rpchost, rpcport, rpcuser, rpcpassword, rpctimeout, threads, nice,
                        nfactor, tip_poll, work_refresh, retry, stats_interval, hugepages
                        ('#' starts a comment only at the start of a line)
  --yacoin-conf FILE    read rpcuser/rpcpassword/rpcport/rpcconnect from a node yacoin.conf
  --rpc-host HOST       default 127.0.0.1
  --rpc-port PORT       default 7687 (mainnet)
  --rpc-user USER       (the password is read only from a config file, never from
                        the command line, so it cannot show up in `ps`)
  --rpc-timeout SEC     default 120 (a submit hashes 512 MiB on the node)

Mining:
  --threads N           worker threads (default 7; 512 MiB each at N-factor 21)
  --nice N              process nice level (default 10; 0 = leave unchanged)
  --nfactor N           expected N-factor (default 21; the test chain uses 4);
                        must match the node's getmininginfo Nfactor
  --tip-poll SEC        getbestblockhash interval (default 5)
  --work-refresh SEC    fetch new work at least this often (default 300)
  --retry SEC           submit retry interval while the node has no peers (default 5)
  --stats-interval SEC  status line interval (default 60)
  --hugepages / --no-hugepages   ask for transparent huge pages (default on)
  --ignore-memory-check start even if threads x scratch exceeds available memory
  --max-blocks N        exit after N accepted blocks (for tests; default 0 = never)

Other modes:
  --benchmark           hash without a node: H/s for --threads at --nfactor
  --bench-seconds SEC   benchmark length (default 60)
  --bench-warmup SEC    do not count hashes that finish in the first SEC
                        seconds of the benchmark (default 0)
  --check-work          fetch ONE getwork, decode and check it, print it, exit.
                        Never submits. Note: a getwork fetch is not read-only on
                        the node (it saves a block template and reserves a key).
  --version, --help
)";
}

static double to_double(const std::string& name, const std::string& v)
{
    try {
        size_t pos;
        double d = std::stod(v, &pos);
        if (pos != v.size()) throw std::invalid_argument("");
        return d;
    } catch (...) {
        throw std::invalid_argument(name + ": not a number: " + v);
    }
}

static long to_long(const std::string& name, const std::string& v)
{
    try {
        size_t pos;
        long l = std::stol(v, &pos);
        if (pos != v.size()) throw std::invalid_argument("");
        return l;
    } catch (...) {
        throw std::invalid_argument(name + ": not an integer: " + v);
    }
}

static bool to_bool(const std::string& name, const std::string& v)
{
    if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
    if (v == "0" || v == "false" || v == "no" || v == "off") return false;
    throw std::invalid_argument(name + ": expected 0/1: " + v);
}

// Miner settings that may come from miner.conf.
static void apply_miner_keys(const std::map<std::string, std::string>& kv, Options& o)
{
    for (auto& [k, v] : kv) {
        if (k == "threads") o.threads = static_cast<int>(to_long(k, v));
        else if (k == "nice") o.nice = static_cast<int>(to_long(k, v));
        else if (k == "nfactor") o.nfactor = static_cast<unsigned>(to_long(k, v));
        else if (k == "tip_poll") o.tip_poll_s = to_double(k, v);
        else if (k == "work_refresh") o.work_refresh_s = to_double(k, v);
        else if (k == "retry") o.retry_s = to_double(k, v);
        else if (k == "stats_interval") o.stats_s = to_double(k, v);
        else if (k == "hugepages") o.huge_pages = to_bool(k, v);
    }
}

static bool file_exists(const std::string& p)
{
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

Options parse_options(int argc, char** argv, bool read_files)
{
    Options o;
    o.conf = default_miner_conf_path();

    // First pass: find config files. Second pass: command line overrides.
    std::vector<std::string> args(argv + 1, argv + argc);
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--conf" && i + 1 < args.size()) o.conf = args[i + 1], o.conf_given = true;
        if (args[i] == "--yacoin-conf" && i + 1 < args.size()) o.yacoin_conf = args[i + 1];
    }
    if (read_files) {
        if (!o.yacoin_conf.empty()) apply_rpc_keys(read_kv_file(o.yacoin_conf, ConfStyle::Node), o.rpc);
        if (o.conf_given || file_exists(o.conf)) {
            auto kv = read_kv_file(o.conf);
            apply_rpc_keys(kv, o.rpc);
            apply_miner_keys(kv, o);
        }
    }

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= args.size()) throw std::invalid_argument(a + " needs a value");
            return args[++i];
        };
        if (a == "--conf" || a == "--yacoin-conf") value();
        else if (a == "--rpc-host") o.rpc.host = value();
        else if (a == "--rpc-port") {
            o.rpc.port = static_cast<int>(to_long(a, value()));
            if (o.rpc.port < 1 || o.rpc.port > 65535) throw std::invalid_argument("--rpc-port must be 1..65535");
        }
        else if (a == "--rpc-user") o.rpc.user = value();
        else if (a == "--rpc-password")
            throw std::invalid_argument("--rpc-password is not supported: put rpcpassword in the config file (mode 600)");
        else if (a == "--rpc-timeout") o.rpc.timeout_s = to_long(a, value());
        else if (a == "--threads") o.threads = static_cast<int>(to_long(a, value()));
        else if (a == "--nice") o.nice = static_cast<int>(to_long(a, value()));
        else if (a == "--nfactor") o.nfactor = static_cast<unsigned>(to_long(a, value()));
        else if (a == "--tip-poll") o.tip_poll_s = to_double(a, value());
        else if (a == "--work-refresh") o.work_refresh_s = to_double(a, value());
        else if (a == "--retry") o.retry_s = to_double(a, value());
        else if (a == "--stats-interval") o.stats_s = to_double(a, value());
        else if (a == "--hugepages") o.huge_pages = true;
        else if (a == "--no-hugepages") o.huge_pages = false;
        else if (a == "--ignore-memory-check") o.ignore_memory_check = true;
        else if (a == "--max-blocks") o.max_blocks = static_cast<int>(to_long(a, value()));
        else if (a == "--benchmark") o.benchmark = true;
        else if (a == "--bench-seconds") o.bench_seconds = to_double(a, value());
        else if (a == "--bench-warmup") o.bench_warmup = to_double(a, value());
        else if (a == "--check-work") o.check_work = true;
        else if (a == "--help" || a == "-h") o.help = true;
        else if (a == "--version") o.version = true;
        else throw std::invalid_argument("unknown option " + a + " (see --help)");
    }

    if (o.threads < 1 || o.threads > kMaxBenchThreads) throw std::invalid_argument("--threads must be 1.." + std::to_string(kMaxBenchThreads));
    if (o.nfactor > 30) throw std::invalid_argument("--nfactor must be 0..30");
    if (o.nice < 0 || o.nice > 19) throw std::invalid_argument("--nice must be 0..19");
    if (o.tip_poll_s < 0.1 || o.work_refresh_s < 1 || o.retry_s < 0.1 || o.stats_s < 1 || o.bench_seconds < 1)
        throw std::invalid_argument("interval too small");
    if (o.work_refresh_s > 3600) throw std::invalid_argument("--work-refresh must be <= 3600 (node template age limit is 5400 s)");
    if (o.bench_warmup < 0 || o.bench_warmup >= o.bench_seconds)
        throw std::invalid_argument("--bench-warmup must be >= 0 and less than --bench-seconds");
    if (o.max_blocks < 0) throw std::invalid_argument("--max-blocks must be >= 0");
    return o;
}

}  // namespace yac
