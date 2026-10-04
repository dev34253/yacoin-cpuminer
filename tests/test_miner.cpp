// yacoin-cpuminer: mining-loop unit tests (task T-04): nonce slicing, stale
// work, target checks and the submit-retry logic, without a real node. MIT licence.
#include <chrono>
#include <cstring>
#include <thread>

#include "testing.h"

#include "miner/miner.h"
#include "miner/nonce.h"
#include "node_sim.h"
#include "util/hex.h"
#include "work/target.h"

using namespace yac;
using yac_test::NodeSim;
using yac_test::SimApi;

static bool wait_until(const std::function<bool()>& cond, double seconds)
{
    auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < end) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cond();
}

TEST(nonce_slices_are_disjoint_and_cover_all)
{
    for (unsigned count = 1; count <= 9; ++count) {
        uint64_t expect_begin = 0;
        for (unsigned i = 0; i < count; ++i) {
            NonceRange r = nonce_slice(i, count);
            CHECK_EQ(r.begin, expect_begin);
            CHECK(r.end > r.begin);
            expect_begin = r.end;
        }
        CHECK_EQ(expect_begin, uint64_t(1) << 32);
    }
}

TEST(lane_batches_cover_the_slice_exactly)
{
    // Lane k of a batch hashes nonce first + k; batches are consecutive and
    // the last one is partial when the slice is not a multiple of the lanes.
    for (unsigned lanes = 1; lanes <= kMaxLanes; ++lanes) {
        for (uint64_t size : {uint64_t(1), uint64_t(7), uint64_t(8), uint64_t(23), uint64_t(64)}) {
            NonceRange r{1000, 1000 + size};
            uint64_t n = r.begin, expect = r.begin, batches = 0;
            unsigned last_count = 0;
            while (true) {
                LaneBatch b = next_batch(n, r, lanes);
                if (b.count == 0) break;
                CHECK(b.count <= lanes);
                for (unsigned k = 0; k < b.count; ++k) CHECK_EQ(uint64_t(b.nonce(k)), expect++);
                last_count = b.count;
                n += b.count;
                ++batches;
            }
            CHECK_EQ(expect, r.end);
            CHECK_EQ(batches, (size + lanes - 1) / lanes);
            CHECK_EQ(uint64_t(last_count), size % lanes ? size % lanes : uint64_t(lanes));
        }
    }
}

TEST(lane_batch_at_the_top_of_the_nonce_space)
{
    // The last thread's slice ends at 2^32: nonces must not wrap.
    NonceRange r{(uint64_t(1) << 32) - 5, uint64_t(1) << 32};
    LaneBatch b = next_batch(r.begin, r, 3);
    CHECK_EQ(b.count, 3u);
    CHECK_EQ(b.nonce(0), 0xfffffffbu);
    b = next_batch(r.begin + 3, r, 3);
    CHECK_EQ(b.count, 2u);
    CHECK_EQ(b.nonce(1), 0xffffffffu);
    CHECK_EQ(next_batch(r.end, r, 3).count, 0u);
}

TEST(verify_solution_rehashes_the_submitted_header)
{
    NodeSim sim(1, 0x2000ffff);
    auto job = std::make_shared<Job>();
    job->id = 1;
    job->work = sim.get_work();
    job->prev_hex = job->work.header.prev_hex();
    ScryptHasher hasher(1);
    uint8_t header[kHeaderSize];
    std::memcpy(header, job->work.header_bytes(), kHeaderSize);
    Solution good, miss;
    bool have_good = false, have_miss = false;
    for (uint32_t n = 0; n < 100000 && !(have_good && have_miss); ++n) {
        set_header_nonce(header, n);
        Hash256 h = hasher.hash(header, kHeaderSize);
        bool meets = hash_meets_target(h, job->work.target);
        Solution& s = meets ? good : miss;
        if (meets ? have_good : have_miss) continue;
        s.job = job;
        s.nonce = n;
        s.hash = h;
        (meets ? have_good : have_miss) = true;
    }
    CHECK(have_good && have_miss);
    ScryptHasher ref(1);
    std::string why;
    CHECK(verify_solution(good, ref, why));
    // A lane-to-nonce mix-up: the right hash reported with another nonce.
    Solution wrong_nonce = good;
    wrong_nonce.nonce ^= 1;
    CHECK(!verify_solution(wrong_nonce, ref, why));
    CHECK(why.find("reference hash") != std::string::npos);
    // A broken optimized hash: wrong bytes reported.
    Solution wrong_hash = good;
    wrong_hash.hash[5] ^= 0x40;
    CHECK(!verify_solution(wrong_hash, ref, why));
    // Correct hash, but it does not meet the target.
    CHECK(!verify_solution(miss, ref, why));
    CHECK(why.find("target") != std::string::npos);
}

// ---------------------------------------------------------------- Submitter

// A scripted NodeApi: a list of tips to return and of submit outcomes.
struct ScriptApi : NodeApi {
    std::vector<std::string> tips;  // successive best_block_hash results (last one repeats)
    size_t tip_i = 0;
    std::vector<int> submit_script;  // 1 = true, 0 = false, negative = throw RpcError(code)
    size_t sub_i = 0;
    int submits = 0;
    std::string best_block_hash() override
    {
        std::string t = tips[std::min(tip_i, tips.size() - 1)];
        ++tip_i;
        return t;
    }
    Work get_work() override { throw std::runtime_error("not used"); }
    bool submit_work(const std::string&) override
    {
        ++submits;
        int v = submit_script[std::min(sub_i, submit_script.size() - 1)];
        ++sub_i;
        if (v < 0) throw RpcError(v, "scripted error");
        return v == 1;
    }
    MiningInfo mining_info() override { return {}; }
    NodeInfo node_info() override { return {}; }
};

static const std::string kParent = "00000aaaa00000000000000000000000000000000000000000000000000000a1";
static const std::string kOther = "00000bbbb00000000000000000000000000000000000000000000000000000b2";

static Solution make_solution(bool meets = true)
{
    auto job = std::make_shared<Job>();
    job->id = 1;
    job->work.target = target_from_compact(0x1e0fffff);
    job->work.plain[84] = 0x80;
    job->work.plain[126] = 0x02;
    job->work.plain[127] = 0xa0;
    job->prev_hex = kParent;
    Solution s;
    s.job = job;
    s.nonce = 42;
    s.hash.fill(0);
    s.hash[0] = 0x55;
    s.hash[29] = meets ? 0x01 : 0x10;  // 0x0000 01.. <= 0x00000fffff.. ; 0x000010.. is above
    return s;
}

static std::string hash_hex(const Solution& s) { return to_hex_reversed(s.hash.data(), 32); }

struct SubmitCase {
    Stats stats{1};
    std::atomic<bool> stop{false};
    int sleeps = 0;
    SubmitResult run(ScriptApi& api, const Solution& s)
    {
        Submitter sub(api, stats, stop, 5, [this](double) { ++sleeps; });
        return sub.submit(s);
    }
};

TEST(submit_accepted)
{
    ScriptApi api;
    api.tips = {kParent};
    api.submit_script = {1};
    SubmitCase c;
    CHECK(c.run(api, make_solution()) == SubmitResult::Accepted);
    CHECK_EQ(api.submits, 1);
    CHECK_EQ(c.stats.retried.load(), uint64_t(0));
}

TEST(submit_not_sent_when_local_target_check_fails)
{
    ScriptApi api;
    api.tips = {kParent};
    api.submit_script = {1};
    SubmitCase c;
    CHECK(c.run(api, make_solution(false)) == SubmitResult::InvalidLocal);
    CHECK_EQ(api.submits, 0);
}

TEST(submit_stale_when_tip_moved_before_submit)
{
    ScriptApi api;
    api.tips = {kOther};
    api.submit_script = {1};
    SubmitCase c;
    CHECK(c.run(api, make_solution()) == SubmitResult::Stale);
    CHECK_EQ(api.submits, 0);
}

TEST(submit_retries_while_no_peers_then_accepted)
{
    ScriptApi api;
    api.tips = {kParent};
    api.submit_script = {kRpcClientNotConnected, kRpcClientInInitialDownload, 1};
    SubmitCase c;
    CHECK(c.run(api, make_solution()) == SubmitResult::Accepted);
    CHECK_EQ(api.submits, 3);
    CHECK_EQ(c.stats.retried.load(), uint64_t(2));
    CHECK_EQ(c.sleeps, 2);
}

TEST(submit_retry_ends_stale_when_tip_changes)
{
    ScriptApi api;
    api.tips = {kParent, kParent, kOther};
    api.submit_script = {kRpcClientNotConnected};
    SubmitCase c;
    CHECK(c.run(api, make_solution()) == SubmitResult::Stale);
    CHECK_EQ(api.submits, 2);
    CHECK_EQ(c.stats.retried.load(), uint64_t(2));
}

TEST(submit_retries_when_wallet_locked)
{
    ScriptApi api;
    api.tips = {kParent};
    api.submit_script = {kRpcWalletLocked, 1};
    SubmitCase c;
    CHECK(c.run(api, make_solution()) == SubmitResult::Accepted);
    CHECK_EQ(c.stats.retried.load(), uint64_t(1));
}

TEST(submit_false_is_classified)
{
    {  // false, tip unchanged: rejected (pointer to debug.log is logged)
        ScriptApi api;
        api.tips = {kParent};
        api.submit_script = {0};
        SubmitCase c;
        CHECK(c.run(api, make_solution()) == SubmitResult::Rejected);
    }
    {  // false, tip moved meanwhile: stale
        ScriptApi api;
        api.tips = {kParent, kOther};
        api.submit_script = {0};
        SubmitCase c;
        CHECK(c.run(api, make_solution()) == SubmitResult::Stale);
    }
    {  // false, but the tip is our block (duplicate submit): accepted
        Solution s = make_solution();
        ScriptApi api;
        api.tips = {kParent, hash_hex(s)};
        api.submit_script = {0};
        SubmitCase c;
        CHECK(c.run(api, s) == SubmitResult::Accepted);
    }
    {  // other RPC error: rejected, no retry
        ScriptApi api;
        api.tips = {kParent};
        api.submit_script = {-8};
        SubmitCase c;
        CHECK(c.run(api, make_solution()) == SubmitResult::Rejected);
        CHECK_EQ(c.stats.retried.load(), uint64_t(0));
    }
}

TEST(submit_stops_retrying_on_stop)
{
    ScriptApi api;
    api.tips = {kParent};
    api.submit_script = {kRpcClientNotConnected};
    SubmitCase c;
    Submitter sub(api, c.stats, c.stop, 5, [&](double) {
        if (++c.sleeps == 3) c.stop = true;
    });
    CHECK(sub.submit(make_solution()) == SubmitResult::Stopped);
}

// ----------------------------------------------------- Miner with NodeSim

static MinerConfig fast_config(unsigned threads)
{
    MinerConfig cfg;
    cfg.threads = threads;
    cfg.nfactor = 1;
    cfg.huge_pages = false;
    cfg.tip_poll_s = 0.1;
    cfg.work_refresh_s = 30;
    cfg.retry_s = 0.1;
    cfg.stats_s = 30;
    return cfg;
}

TEST(miner_finds_and_submits_blocks)
{
    auto sim = std::make_shared<NodeSim>(1, 0x2000ffff);  // ~256 hashes per block
    std::atomic<bool> stop{false};
    MinerConfig cfg = fast_config(3);
    cfg.max_blocks = 5;
    Miner m(cfg, [sim] { return std::make_unique<SimApi>(sim); }, stop);
    std::thread t([&] { CHECK_EQ(m.run(), 0); });
    bool done = wait_until([&] { return stop.load(); }, 60);
    stop = true;
    t.join();
    CHECK(done);
    CHECK_EQ(sim->accepted().size(), size_t(5));
    CHECK_EQ(m.stats().accepted.load(), uint64_t(5));
    CHECK_EQ(m.stats().rejected.load(), uint64_t(0));
    CHECK(m.stats().found.load() >= 5);
}

TEST(miner_with_lanes_finds_and_submits_blocks)
{
    // 3 lanes per thread; every solution passes the reference re-hash before
    // the submit (verify_failed stays 0) and the node accepts it.
    auto sim = std::make_shared<NodeSim>(1, 0x2000ffff);
    std::atomic<bool> stop{false};
    MinerConfig cfg = fast_config(2);
    cfg.lanes = 3;
    cfg.max_blocks = 5;
    Miner m(cfg, [sim] { return std::make_unique<SimApi>(sim); }, stop);
    std::thread t([&] { CHECK_EQ(m.run(), 0); });
    bool done = wait_until([&] { return stop.load(); }, 60);
    stop = true;
    t.join();
    CHECK(done);
    CHECK_EQ(sim->accepted().size(), size_t(5));
    CHECK_EQ(m.stats().accepted.load(), uint64_t(5));
    CHECK_EQ(m.stats().rejected.load(), uint64_t(0));
    CHECK_EQ(m.stats().verify_failed.load(), uint64_t(0));
}

TEST(miner_drops_stale_work_when_tip_changes)
{
    // Target 0 (nBits 0x01000000): never solvable, so only the tip logic acts.
    auto sim = std::make_shared<NodeSim>(1, 0x01000000);
    std::atomic<bool> stop{false};
    Miner m(fast_config(2), [sim] { return std::make_unique<SimApi>(sim); }, stop);
    std::thread t([&] { m.run(); });
    CHECK(wait_until([&] { return m.stats().work_fetched.load() >= 1 && m.stats().total_hashes() > 100; }, 20));
    int calls_before = sim->getwork_calls();
    sim->external_block();  // another miner found a block
    // New work on the new tip within a few tip polls; hashing goes on.
    CHECK(wait_until([&] { return sim->getwork_calls() > calls_before; }, 5));
    uint64_t h = m.stats().total_hashes();
    CHECK(wait_until([&] { return m.stats().total_hashes() > h + 100; }, 5));
    stop = true;
    t.join();
    CHECK_EQ(m.stats().accepted.load(), uint64_t(0));
    CHECK_EQ(sim->submit_calls(), 0);
    CHECK_EQ(sim->getwork_calls(), calls_before + 1);  // exactly one new fetch for the new tip
}

TEST(miner_waits_for_peers_then_mines)
{
    auto sim = std::make_shared<NodeSim>(1, 0x2000ffff);
    sim->set_peers(false);
    std::atomic<bool> stop{false};
    MinerConfig cfg = fast_config(2);
    cfg.max_blocks = 2;
    Miner m(cfg, [sim] { return std::make_unique<SimApi>(sim); }, stop);
    std::thread t([&] { m.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    CHECK_EQ(m.stats().work_fetched.load(), uint64_t(0));  // getwork refused: no work, no hashing
    CHECK_EQ(m.stats().total_hashes(), uint64_t(0));
    sim->set_peers(true);
    CHECK(wait_until([&] { return stop.load(); }, 60));
    stop = true;
    t.join();
    CHECK_EQ(sim->accepted().size(), size_t(2));
}

TEST(miner_retries_refused_submit_until_accepted)
{
    auto sim = std::make_shared<NodeSim>(1, 0x2000ffff);
    sim->refuse_next_submits(3);  // "not connected" three times (plan §7.5 in miniature)
    std::atomic<bool> stop{false};
    MinerConfig cfg = fast_config(2);
    cfg.max_blocks = 1;
    Miner m(cfg, [sim] { return std::make_unique<SimApi>(sim); }, stop);
    std::thread t([&] { m.run(); });
    CHECK(wait_until([&] { return stop.load(); }, 60));
    stop = true;
    t.join();
    CHECK_EQ(sim->accepted().size(), size_t(1));
    CHECK(m.stats().retried.load() >= 3);
}

TEST(miner_limits_getwork_calls_when_tip_is_unchanged)
{
    // F12: each getwork saves a block and reserves a key. With an unchanged
    // tip and nothing found, 20+ tip polls must not cause more fetches.
    auto sim = std::make_shared<NodeSim>(1, 0x01000000);  // target 0: never solved
    std::atomic<bool> stop{false};
    Miner m(fast_config(2), [sim] { return std::make_unique<SimApi>(sim); }, stop);
    std::thread t([&] { m.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));  // ~25 polls at 0.1 s
    stop = true;
    t.join();
    CHECK_EQ(sim->getwork_calls(), 1);
    CHECK(m.stats().total_hashes() > 0);
}

TEST(miner_recovers_when_node_lost_its_saved_block)
{
    // The node restarts after handing out work: the submit returns false
    // ("No saved block"), the miner must fetch new work on the same tip.
    auto sim = std::make_shared<NodeSim>(1, 0x2000ffff);
    sim->restart_after_next_getwork();
    std::atomic<bool> stop{false};
    MinerConfig cfg = fast_config(2);
    cfg.max_blocks = 1;
    Miner m(cfg, [sim] { return std::make_unique<SimApi>(sim); }, stop);
    std::thread t([&] { m.run(); });
    CHECK(wait_until([&] { return stop.load(); }, 60));
    stop = true;
    t.join();
    CHECK_EQ(sim->accepted().size(), size_t(1));
    CHECK(m.stats().rejected.load() >= 1);
    CHECK(sim->getwork_calls() >= 2);
}

TEST(miner_retries_submit_while_node_unreachable)
{
    auto sim = std::make_shared<NodeSim>(1, 0x2000ffff);
    sim->unreachable_next_submits(2);
    std::atomic<bool> stop{false};
    MinerConfig cfg = fast_config(2);
    cfg.max_blocks = 1;
    Miner m(cfg, [sim] { return std::make_unique<SimApi>(sim); }, stop);
    std::thread t([&] { m.run(); });
    CHECK(wait_until([&] { return stop.load(); }, 60));
    stop = true;
    t.join();
    CHECK_EQ(sim->accepted().size(), size_t(1));
    CHECK(m.stats().retried.load() >= 2);
    CHECK_EQ(m.stats().rejected.load(), uint64_t(0));
}

TEST(benchmark_runs_without_node)
{
    std::atomic<bool> stop{false};
    std::vector<double> per;
    double total = run_benchmark(2, 4, 1.0, false, per, stop);
    CHECK_EQ(per.size(), size_t(2));
    CHECK(total > 0);
    CHECK(per[0] > 0 && per[1] > 0);
}

TEST(benchmark_with_lanes_and_warmup)
{
    std::atomic<bool> stop{false};
    std::vector<double> per;
    double total = run_benchmark(2, 4, 1.5, false, per, stop, 0.5, 3);
    CHECK_EQ(per.size(), size_t(2));
    CHECK(total > 0);
}

TEST_MAIN()
