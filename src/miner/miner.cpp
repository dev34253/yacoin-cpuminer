// yacoin-cpuminer: worker threads, work coordination and submission. MIT licence.
#include "miner/miner.h"

#include <algorithm>
#include <cstring>
#include <thread>

#include "miner/nonce.h"
#include "util/hex.h"
#include "util/log.h"
#include "work/target.h"

namespace yac {

uint64_t Stats::total_hashes() const
{
    uint64_t t = 0;
    for (auto& h : hashes) t += h.load(std::memory_order_relaxed);
    return t;
}

// ---------------------------------------------------------------- JobBoard

void JobBoard::publish(std::shared_ptr<const Job> job)
{
    {
        std::lock_guard<std::mutex> l(mu_);
        job_ = std::move(job);
        gen_.fetch_add(1, std::memory_order_acq_rel);
    }
    cv_.notify_all();
}

void JobBoard::withdraw(uint64_t job_id)
{
    {
        std::lock_guard<std::mutex> l(mu_);
        if (!job_ || (job_id != 0 && job_->id != job_id)) return;
        job_.reset();
        gen_.fetch_add(1, std::memory_order_acq_rel);
    }
    cv_.notify_all();
}

std::shared_ptr<const Job> JobBoard::current() const
{
    std::lock_guard<std::mutex> l(mu_);
    return job_;
}

std::shared_ptr<const Job> JobBoard::wait_for_new(uint64_t seen_gen, uint64_t& gen_out, const std::atomic<bool>& stop)
{
    std::unique_lock<std::mutex> l(mu_);
    while (!stop.load() && gen_.load() == seen_gen) cv_.wait_for(l, std::chrono::milliseconds(200));
    gen_out = gen_.load();
    return job_;
}

void JobBoard::wake_all() { cv_.notify_all(); }

// --------------------------------------------------------------- Submitter

const char* to_string(SubmitResult r)
{
    switch (r) {
    case SubmitResult::Accepted: return "accepted";
    case SubmitResult::Rejected: return "rejected";
    case SubmitResult::Stale: return "stale";
    case SubmitResult::InvalidLocal: return "invalid (local check)";
    case SubmitResult::Stopped: return "not submitted (stopping)";
    }
    return "?";
}

Submitter::Submitter(NodeApi& api, Stats& stats, const std::atomic<bool>& stop, double retry_s, SleepFn sleep)
    : api_(api), stats_(stats), stop_(stop), retry_s_(retry_s), sleep_(std::move(sleep))
{
}

SubmitResult Submitter::submit(const Solution& s)
{
    const Work& w = s.job->work;
    // 1. Local check (plan §5): never send something the node will refuse for PoW.
    if (!hash_meets_target(s.hash, w.target)) return SubmitResult::InvalidLocal;

    const std::string our_hash = to_hex_reversed(s.hash.data(), 32);  // v7 block hash = PoW hash
    const std::string data = encode_getwork_submit(w, s.nonce);
    bool announced_retry = false;
    bool announced_locked = false;

    for (bool first = true; ; first = false) {
        if (!first) {
            if (stop_) return SubmitResult::Stopped;
            stats_.retried++;
            sleep_(retry_s_);
            if (stop_) return SubmitResult::Stopped;
        }
        // 2. Stale check: is our parent still the tip?
        std::string tip;
        try {
            tip = api_.best_block_hash();
        } catch (const RpcAuthError&) {
            throw;
        } catch (const std::exception& e) {
            if (!announced_retry) log_warn(std::string("submit: cannot read the tip (") + e.what() + "); retrying");
            announced_retry = true;
            continue;  // node unreachable: keep the solution and retry
        }
        if (tip == our_hash) return SubmitResult::Accepted;  // already in (e.g. after a timeout)
        if (tip != s.job->prev_hex) return SubmitResult::Stale;

        // 3. Submit.
        bool ok = false;
        try {
            stats_.submitted++;
            ok = api_.submit_work(data);
        } catch (const std::exception& e) {
            switch (classify_rpc_failure(e)) {
            case RpcFailure::Auth:
                throw;
            case RpcFailure::Transient:
                // No peers / initial download / node unreachable (F8): the node
                // keeps the saved block until the tip changes (F9), so retry.
                if (!announced_retry)
                    log_warn(std::string("submit refused (") + e.what() + "); keeping the solution and retrying every " +
                             fmt_double(retry_s_, 0) + " s until accepted or the tip changes");
                announced_retry = true;
                continue;
            case RpcFailure::WalletLocked:
                if (!announced_locked)
                    log_error(std::string("submit: ") + e.what() +
                              ": unlock the node's wallet (walletpassphrase); retrying until the tip changes");
                announced_locked = true;
                continue;
            case RpcFailure::Other:
                log_error(std::string("submit failed: ") + e.what());
                return SubmitResult::Rejected;
            }
        }
        if (ok) return SubmitResult::Accepted;

        // 4. Plain false has four causes (F11). Tell them apart where we can.
        try {
            std::string t2 = api_.best_block_hash();
            if (t2 == our_hash) return SubmitResult::Accepted;
            if (t2 != s.job->prev_hex) return SubmitResult::Stale;
        } catch (const RpcAuthError&) {
            throw;
        } catch (const std::exception&) {
        }
        log_error("node returned false for block " + our_hash +
                  " (no saved block, PoW, stale or block rejected): see the node's debug.log, lines "
                  "'rpc getwork' / 'CheckWork' / 'ProcessNewBlock'");
        return SubmitResult::Rejected;
    }
}

// ------------------------------------------------------------------- Miner

Miner::Miner(MinerConfig cfg, ApiFactory make_api, std::atomic<bool>& stop)
    : cfg_(cfg), make_api_(std::move(make_api)), stop_(stop), stats_(cfg.threads)
{
}

void Miner::fatal(const std::string& msg)
{
    log_error(msg);
    fatal_ = true;
    stop_ = true;
}

void Miner::request_refresh(bool force)
{
    if (force) force_fetch_ = true;
    refresh_requested_ = true;
    std::lock_guard<std::mutex> l(wake_mu_);
    wake_cv_.notify_all();
}

void Miner::worker(unsigned index)
{
    if (stop_) return;  // stopped during start-up: do not allocate 512 MiB
    std::unique_ptr<ScryptHasher> hasher;
    try {
        hasher = std::make_unique<ScryptHasher>(cfg_.nfactor, cfg_.huge_pages);
    } catch (const std::exception& e) {
        fatal("worker " + std::to_string(index) + ": " + e.what());
        return;
    }
    const NonceRange range = nonce_slice(index, cfg_.threads);
    uint8_t header[kHeaderSize];
    Hash256 h;
    uint64_t seen_gen = 0;
    while (!stop_) {
        uint64_t gen;
        std::shared_ptr<const Job> job = board_.wait_for_new(seen_gen, gen, stop_);
        seen_gen = gen;
        if (!job || stop_) continue;  // withdrawn: wait for the next one
        std::memcpy(header, job->work.header_bytes(), kHeaderSize);
        uint64_t n = range.begin;
        for (; n < range.end; ++n) {
            // New or withdrawn work: drop this job at once (plan §5).
            if (stop_.load(std::memory_order_relaxed) || board_.generation() != gen) break;
            set_header_nonce(header, static_cast<uint32_t>(n));
            hasher->hash(header, kHeaderSize, h);
            stats_.hashes[index].fetch_add(1, std::memory_order_relaxed);
            if (hash_meets_target(h, job->work.target)) {
                stats_.found++;
                {
                    std::lock_guard<std::mutex> l(sol_mu_);
                    solutions_.push_back(Solution{job, static_cast<uint32_t>(n), h});
                }
                sol_cv_.notify_one();
            }
        }
        if (n >= range.end) {
            log_warn("worker " + std::to_string(index) + " scanned its whole nonce slice; asking for new work");
            request_refresh(true);
        }
    }
}

void Miner::submitter_loop()
{
    std::unique_ptr<NodeApi> api;
    try {
        api = make_api_();
    } catch (const std::exception& e) {
        fatal(std::string("submitter: ") + e.what());
        return;
    }
    auto sleeper = [this](double s) {
        auto until = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(s));
        while (!stop_ && Clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    };
    Submitter sub(*api, stats_, stop_, cfg_.retry_s, sleeper);
    std::string dead_prev;  // parent hash that is no longer the tip: drop its solutions
    while (!stop_) {
        Solution s;
        {
            std::unique_lock<std::mutex> l(sol_mu_);
            sol_cv_.wait_for(l, std::chrono::milliseconds(200), [&] { return !solutions_.empty() || stop_.load(); });
            if (solutions_.empty()) continue;
            s = std::move(solutions_.front());
            solutions_.pop_front();
        }
        if (!dead_prev.empty() && s.job->prev_hex == dead_prev) {
            stats_.dropped++;
            continue;
        }
        std::string hash_hex = to_hex_reversed(s.hash.data(), 32);
        log_info("FOUND block " + hash_hex + " (nonce " + std::to_string(s.nonce) + ", job " +
                 std::to_string(s.job->id) + "); submitting");
        SubmitResult r;
        try {
            r = sub.submit(s);
        } catch (const std::exception& e) {
            fatal(std::string("submit: ") + e.what());
            return;
        }
        switch (r) {
        case SubmitResult::Accepted:
            stats_.accepted++;
            log_info("ACCEPTED block " + hash_hex + " on parent " + s.job->prev_hex);
            dead_prev = s.job->prev_hex;
            if (auto cur = board_.current(); cur && cur->prev_hex == dead_prev) board_.withdraw(cur->id);
            request_refresh();
            if (cfg_.max_blocks > 0 && stats_.accepted.load() >= static_cast<uint64_t>(cfg_.max_blocks)) {
                log_info("reached --max-blocks " + std::to_string(cfg_.max_blocks) + "; stopping");
                stop_ = true;
            }
            break;
        case SubmitResult::Stale:
            stats_.stale++;
            log_info("STALE block " + hash_hex + ": the tip moved before the submit");
            dead_prev = s.job->prev_hex;
            request_refresh();
            break;
        case SubmitResult::Rejected:
            stats_.rejected++;
            request_refresh(true);  // e.g. "No saved block" after a node restart (F11)
            break;
        case SubmitResult::InvalidLocal:
            stats_.rejected++;
            log_error("BUG: solution " + hash_hex + " fails the local target check; not submitted");
            break;
        case SubmitResult::Stopped:
            log_warn("stopping with an unsubmitted solution " + hash_hex);
            break;
        }
    }
}

bool Miner::fetch_allowed(NodeApi& api)
{
    // F12: every getwork reserves a wallet key; with an encrypted, locked wallet
    // and an empty keypool the node builds a block with a null payout script
    // and may crash. Check before every fetch (the wallet may relock later).
    NodeInfo ni = api.node_info();
    bool locked = ni.unlocked_until && *ni.unlocked_until == 0;
    if (locked && ni.keypoolsize == 0) {
        if (!fetch_blocked_logged_)
            log_error("the node's wallet is locked and its keypool is empty: not calling getwork (it could crash "
                      "the node, plan F12). Unlock the wallet or run keypoolrefill on the node.");
        fetch_blocked_logged_ = true;
        return false;
    }
    if (locked && !locked_warned_) {
        log_warn("the node's wallet is locked: found blocks cannot be signed (error -100) until it is unlocked");
        locked_warned_ = true;
    }
    if (!locked) locked_warned_ = false;
    fetch_blocked_logged_ = false;
    return true;
}

bool Miner::fetch_work(NodeApi& api, const std::string& tip)
{
    Work w = api.get_work();  // throws on RPC errors, UnsupportedWork, bad data
    auto job = std::make_shared<Job>();
    job->id = next_job_id_++;
    job->work = std::move(w);
    job->prev_hex = job->work.header.prev_hex();
    job->fetched = Clock::now();
    stats_.work_fetched++;
    if (job->prev_hex != tip)
        log_info("note: work is on " + job->prev_hex + ", tip poll said " + tip + " (tip moved in between)");
    char bits[16];
    std::snprintf(bits, sizeof bits, "%08x", job->work.header.bits);
    log_info("new work " + std::to_string(job->id) + ": parent " + job->prev_hex + ", time " +
             std::to_string(job->work.header.time) + ", bits " + bits);
    board_.publish(std::move(job));
    return true;
}

std::string Miner::stats_line(double interval_s, const std::vector<uint64_t>& prev) const
{
    double total = 0;
    std::string per;
    for (size_t i = 0; i < stats_.hashes.size(); ++i) {
        double r = (stats_.hashes[i].load() - prev[i]) / interval_s;
        total += r;
        per += (i ? " " : "") + fmt_double(r, 2);
    }
    std::string eta;
    if (auto job = board_.current(); job && total > 0) {
        double hours = expected_hashes(job->work.target) / total / 3600.0;
        eta = ", expected time per block " + fmt_double(hours, hours < 10 ? 2 : 1) + " h";
    }
    return "rate " + fmt_double(total, 2) + " H/s [" + per + "]" + eta + "; work " +
           std::to_string(stats_.work_fetched.load()) + ", found " + std::to_string(stats_.found.load()) +
           ", accepted " + std::to_string(stats_.accepted.load()) + ", rejected " +
           std::to_string(stats_.rejected.load()) + ", stale " + std::to_string(stats_.stale.load()) +
           ", retried " + std::to_string(stats_.retried.load()) + ", dropped " +
           std::to_string(stats_.dropped.load()) + ", hashes " + std::to_string(stats_.total_hashes());
}

int Miner::run()
{
    std::unique_ptr<NodeApi> api = make_api_();
    std::vector<std::thread> threads;
    for (unsigned i = 0; i < cfg_.threads; ++i) threads.emplace_back([this, i] { worker(i); });
    std::thread submit_thread([this] { submitter_loop(); });

    const auto dur = [](double s) { return std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(s)); };
    auto start = Clock::now();
    auto next_poll = start;
    auto last_stats = start;
    std::vector<uint64_t> prev_hashes(cfg_.threads, 0);
    std::string tip;
    std::string last_error;
    bool recovering = false;
    auto last_error_time = start - std::chrono::hours(1);

    while (!stop_) {
        {
            std::unique_lock<std::mutex> l(wake_mu_);
            auto until = std::min({next_poll, last_stats + dur(cfg_.stats_s), Clock::now() + std::chrono::milliseconds(200)});
            wake_cv_.wait_until(l, until, [&] { return stop_.load() || refresh_requested_.load(); });
        }
        if (stop_) break;
        auto now = Clock::now();
        if (now >= last_stats + dur(cfg_.stats_s)) {
            double secs = std::chrono::duration<double>(now - last_stats).count();
            log_info(stats_line(secs, prev_hashes));
            for (unsigned i = 0; i < cfg_.threads; ++i) prev_hashes[i] = stats_.hashes[i].load();
            last_stats = now;
        }
        bool refresh = refresh_requested_.exchange(false);
        bool force = force_fetch_.exchange(false);
        if (!refresh && now < next_poll) continue;
        next_poll = now + dur(cfg_.tip_poll_s);
        try {
            std::string t = api->best_block_hash();
            auto job = board_.current();
            if (t != tip) {
                if (job && job->prev_hex != t) {
                    board_.withdraw(job->id);  // workers drop stale work at once
                    log_info("tip changed to " + t + "; dropped old work");
                    job.reset();
                }
                tip = t;
            }
            bool too_old = job && now - job->fetched >= dur(cfg_.work_refresh_s);
            if (!job || job->prev_hex != tip || too_old || force || recovering) {
                if (fetch_allowed(*api)) {
                    fetch_work(*api, tip);
                    recovering = false;
                } else if (auto j = board_.current()) {
                    board_.withdraw(j->id);
                }
            }
            last_error.clear();
        } catch (const UnsupportedWork& e) {
            fatal(e.what());
        } catch (const std::exception& e) {
            // After any error the node may have restarted and lost its saved
            // blocks (F9): fetch fresh work once it answers again.
            recovering = true;
            if (force) force_fetch_ = true;
            if (classify_rpc_failure(e) == RpcFailure::Auth) {
                fatal(e.what());
            } else {
                std::string msg = e.what();
                if (msg != last_error || now - last_error_time > std::chrono::minutes(5)) {
                    log_warn(msg + (board_.current() ? " (still mining the current work)" : " (no work; waiting)"));
                    last_error = msg;
                    last_error_time = now;
                }
            }
        }
    }

    stop_ = true;
    board_.wake_all();
    sol_cv_.notify_all();
    for (auto& t : threads) t.join();
    submit_thread.join();
    double secs = std::chrono::duration<double>(Clock::now() - start).count();
    std::vector<uint64_t> zero(cfg_.threads, 0);
    log_info("final: " + stats_line(secs > 0 ? secs : 1, zero) + ", uptime " + fmt_double(secs, 0) + " s");
    return fatal_ ? 1 : 0;
}

// --------------------------------------------------------------- benchmark

double run_benchmark(unsigned threads, unsigned nfactor, double seconds, bool huge_pages,
                     std::vector<double>& per_thread, const std::atomic<bool>& stop)
{
    std::atomic<unsigned> ready{0};
    std::atomic<bool> go{false};
    std::atomic<bool> done{false};
    std::vector<uint64_t> counts(threads, 0);
    std::vector<double> elapsed(threads, 0);
    std::vector<std::string> errors(threads);
    std::vector<std::thread> ts;
    for (unsigned i = 0; i < threads; ++i) {
        ts.emplace_back([&, i] {
            try {
                ScryptHasher hasher(nfactor, huge_pages);
                uint8_t header[kHeaderSize];
                for (size_t j = 0; j < kHeaderSize; ++j) header[j] = static_cast<uint8_t>(j * 13 + i);
                Hash256 h;
                hasher.hash(header, kHeaderSize, h);  // warm-up: touches all scratch pages
                ready++;
                while (!go && !stop) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                auto t0 = Clock::now();
                uint32_t nonce = 0;
                while (!done && !stop) {
                    set_header_nonce(header, nonce++);
                    hasher.hash(header, kHeaderSize, h);
                    if (done) break;  // finished after the deadline: not counted
                    counts[i]++;
                    elapsed[i] = std::chrono::duration<double>(Clock::now() - t0).count();
                }
            } catch (const std::exception& e) {
                errors[i] = e.what();
                ready++;
            }
        });
    }
    while (ready < threads && !stop) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    go = true;
    auto t0 = Clock::now();
    while (!stop && std::chrono::duration<double>(Clock::now() - t0).count() < seconds)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    done = true;
    for (auto& t : ts) t.join();
    for (auto& e : errors)
        if (!e.empty()) throw std::runtime_error("benchmark: " + e);
    per_thread.assign(threads, 0);
    double total = 0;
    for (unsigned i = 0; i < threads; ++i) {
        per_thread[i] = elapsed[i] > 0 ? counts[i] / elapsed[i] : 0;
        total += per_thread[i];
    }
    return total;
}

}  // namespace yac
