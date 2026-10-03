// yacoin-cpuminer: worker threads, work coordination and submission (plan §5, T-04). MIT licence.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hash/pow_hash.h"
#include "rpc/node_api.h"
#include "work/getwork.h"

namespace yac {

using Clock = std::chrono::steady_clock;

// One fetched getwork, shared read-only by all workers.
struct Job {
    uint64_t id = 0;  // increases with every fetch
    Work work;
    std::string prev_hex;  // as getbestblockhash prints it
    Clock::time_point fetched;
};

struct Solution {
    std::shared_ptr<const Job> job;
    uint32_t nonce = 0;
    Hash256 hash{};
};

// Counters, updated from several threads.
struct Stats {
    explicit Stats(unsigned threads) : hashes(threads) {}
    std::vector<std::atomic<uint64_t>> hashes;  // per worker thread
    std::atomic<uint64_t> work_fetched{0};
    std::atomic<uint64_t> found{0};      // solutions found by workers (hash <= target)
    std::atomic<uint64_t> submitted{0};  // getwork <data> calls made
    std::atomic<uint64_t> accepted{0};
    std::atomic<uint64_t> rejected{0};   // node said false (or error) while our work was current
    std::atomic<uint64_t> stale{0};      // tip changed before (or during) the submit
    std::atomic<uint64_t> retried{0};    // submit attempts repeated (no peers, IBD, node down, wallet locked)
    std::atomic<uint64_t> dropped{0};    // solutions for work already superseded, never submitted

    uint64_t total_hashes() const;
};

// The current job, published by the coordinator and read by the workers.
class JobBoard {
public:
    void publish(std::shared_ptr<const Job> job);
    // Withdraws the job if it is still `job_id` (0 = whatever is current), so
    // workers stop at once (tip changed, block accepted).
    void withdraw(uint64_t job_id = 0);
    std::shared_ptr<const Job> current() const;
    // Generation changes on every publish/withdraw; workers poll it per hash.
    uint64_t generation() const { return gen_.load(std::memory_order_acquire); }
    // Blocks until there is a job with generation != seen_gen, or stop.
    std::shared_ptr<const Job> wait_for_new(uint64_t seen_gen, uint64_t& gen_out, const std::atomic<bool>& stop);
    void wake_all();

private:
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::shared_ptr<const Job> job_;
    std::atomic<uint64_t> gen_{0};
};

enum class SubmitResult { Accepted, Rejected, Stale, InvalidLocal, Stopped };
const char* to_string(SubmitResult r);

// Submits one solution with the plan's rules (§5, F7, F8, F10, F11):
// local target check first; stale if the tip moved; retry while the node has
// no peers / is in initial download / unreachable / wallet locked, until
// accepted or the tip changes; a plain `false` is checked against the tip
// (our own hash as tip = accepted, moved tip = stale, else rejected).
class Submitter {
public:
    using SleepFn = std::function<void(double seconds)>;
    Submitter(NodeApi& api, Stats& stats, const std::atomic<bool>& stop, double retry_s, SleepFn sleep);
    SubmitResult submit(const Solution& s);

private:
    NodeApi& api_;
    Stats& stats_;
    const std::atomic<bool>& stop_;
    double retry_s_;
    SleepFn sleep_;
};

struct MinerConfig {
    unsigned threads = 1;
    unsigned nfactor = 21;
    bool huge_pages = true;
    double tip_poll_s = 5;
    double work_refresh_s = 300;
    double retry_s = 5;
    double stats_s = 60;
    int max_blocks = 0;  // stop after this many accepted (0 = never)
};

class Miner {
public:
    using ApiFactory = std::function<std::unique_ptr<NodeApi>()>;
    Miner(MinerConfig cfg, ApiFactory make_api, std::atomic<bool>& stop);
    // Runs until `stop` is set (signal, max_blocks, fatal error). Returns 0 on
    // a clean stop, 1 on a fatal error (auth failure, unsupported work).
    int run();
    const Stats& stats() const { return stats_; }
    std::string stats_line(double interval_s, const std::vector<uint64_t>& prev_hashes) const;

private:
    void worker(unsigned index);
    void submitter_loop();
    bool fetch_work(NodeApi& api, const std::string& tip);
    bool fetch_allowed(NodeApi& api);  // wallet/keypool check before each getwork (F12)
    // Soft: re-check the tip now and fetch only if there is no current work on
    // it. Forced: fetch new work even on the same tip (nonce slice exhausted,
    // a submit rejected, the node came back after an error: its saved blocks
    // may be gone).
    void request_refresh(bool force = false);
    void fatal(const std::string& msg);

    MinerConfig cfg_;
    ApiFactory make_api_;
    std::atomic<bool>& stop_;
    Stats stats_;
    JobBoard board_;
    std::atomic<uint64_t> next_job_id_{1};
    std::atomic<bool> refresh_requested_{false};
    std::atomic<bool> force_fetch_{false};
    std::atomic<bool> fatal_{false};
    bool fetch_blocked_logged_ = false;  // coordinator thread only
    bool locked_warned_ = false;         // coordinator thread only
    std::mutex wake_mu_;
    std::condition_variable wake_cv_;  // wakes the coordinator early

    std::mutex sol_mu_;
    std::condition_variable sol_cv_;
    std::deque<Solution> solutions_;
};

// Benchmark without a node: `threads` workers hash for `seconds` at `nfactor`.
// Returns total H/s; per-thread rates go to `per_thread`.
double run_benchmark(unsigned threads, unsigned nfactor, double seconds, bool huge_pages,
                     std::vector<double>& per_thread, const std::atomic<bool>& stop);

}  // namespace yac
