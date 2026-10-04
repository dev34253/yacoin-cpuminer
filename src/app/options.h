// yacoin-cpuminer: command-line and config-file options. MIT licence.
#pragma once

#include <optional>
#include <string>

#include "rpc/config.h"

namespace yac {

// Owner decision Q5: leave one of the laptop's 8 hardware threads free.
constexpr int kDefaultThreads = 7;
constexpr int kMaxBenchThreads = 64;
// Hashes per worker thread per call (T-09); chosen from the T-09 benchmarks.
constexpr int kDefaultLanes = 2;

struct Options {
    // Where settings come from: defaults < config files < command line.
    std::string conf;           // miner.conf (default ~/.config/yacoin-cpuminer/miner.conf)
    bool conf_given = false;    // --conf used: the file must exist
    std::string yacoin_conf;    // optional node yacoin.conf (rpcuser/rpcpassword/rpcport/rpcconnect)
    RpcSettings rpc;

    int threads = kDefaultThreads;
    int lanes = kDefaultLanes;
    std::string prefetch = "t0";  // lanes: prefetch hint for the next chunk (t0, nta, none)
    std::string mix = "auto";     // lanes: auto (fused2 if compiled in), plain, fused2, fused4 (T-10)
    int nice = 10;
    unsigned nfactor = 21;
    double tip_poll_s = 5;       // getbestblockhash interval
    double work_refresh_s = 300; // getwork at least this often (node template age limit is 5400 s, F9)
    double retry_s = 5;          // submit retry interval while the node has no peers (F8)
    double stats_s = 60;
    bool huge_pages = true;
    bool ignore_memory_check = false;
    int max_blocks = 0;          // stop after this many accepted blocks (0 = never; for tests)

    bool benchmark = false;
    double bench_seconds = 60;
    double bench_warmup = 0;     // seconds at the start of a benchmark not counted
    bool check_work = false;     // fetch one getwork, decode and print it, never submit
    bool help = false;
    bool version = false;
};

// Parses argv. Throws std::invalid_argument with a message on bad input.
// Reads the config files named by --conf/--yacoin-conf (or the default
// miner.conf when it exists) unless `read_files` is false.
Options parse_options(int argc, char** argv, bool read_files = true);

std::string usage();

}  // namespace yac
