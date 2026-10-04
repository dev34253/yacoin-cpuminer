// yacoin-cpuminer: known-answer tests for the PoW hash (plan §7.1, task T-02).
//
// tests/data/mainnet_headers.json holds real mainnet version-7 headers taken
// with read-only RPC (`getblockheader <hash> false`): the first block after
// the 1,890,000 fork, the fork block itself, two in between and the snapshot
// tip 1,964,617. For each one, the scrypt hash at N-factor 21 of the header
// rebuilt from its fields must equal the block hash. This proves F1-F3.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "testing.h"

#include "hash/pow_hash.h"
#include "hash/yac_scrypt.h"
#include "json.hpp"
#include "util/hex.h"
#include "work/header.h"
#include "work/target.h"

using namespace yac;
using json = nlohmann::json;

static json load(const char* name)
{
    std::ifstream f(std::string(YAC_TEST_DATA_DIR) + "/" + name);
    if (!f) throw std::runtime_error(std::string("cannot open test data ") + name);
    return json::parse(f);
}

static Hash256 hash_from_display(const std::string& hex)
{
    auto v = from_hex(hex);
    Hash256 h{};
    for (size_t i = 0; i < 32; ++i) h[i] = v[31 - i];
    return h;
}

static double seconds_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

TEST(self_test_runs_first)
{
    // Must run before any other hashing (plan F4).
    CHECK(scrypt_self_test());
    std::printf("    scrypt variant: %s\n", scrypt_variant().c_str());
}

TEST(scratch_sizes)
{
    CHECK_EQ(scratch_bytes(21), (size_t(512) << 20) + 256);
    CHECK_EQ(scratch_bytes(4), size_t(4096) + 256);
    CHECK_THROWS(scratch_bytes(31));
}

TEST(header_fields_rebuild_exact_bytes)
{
    json d = load("mainnet_headers.json");
    for (auto& v : d["vectors"]) {
        BlockHeader h;
        h.version = v["version"].get<uint32_t>();
        h.prev_block = hash_from_display(v["previousblockhash"].get<std::string>());
        h.merkle_root = hash_from_display(v["merkleroot"].get<std::string>());
        h.time = v["time"].get<int64_t>();
        h.bits = static_cast<uint32_t>(std::stoul(v["bits"].get<std::string>(), nullptr, 16));
        h.nonce = v["nonce"].get<uint32_t>();
        auto bytes = h.serialize();
        CHECK_EQ(to_hex(bytes.data(), bytes.size()), v["header_hex"].get<std::string>());

        auto parsed = BlockHeader::parse(from_hex(v["header_hex"].get<std::string>()).data());
        CHECK_EQ(parsed.prev_hex(), v["previousblockhash"].get<std::string>());
        CHECK_EQ(parsed.merkle_hex(), v["merkleroot"].get<std::string>());
        CHECK_EQ(parsed.time, v["time"].get<int64_t>());
        CHECK_EQ(parsed.nonce, v["nonce"].get<uint32_t>());
    }
}

TEST(known_answer_mainnet_nfactor21)
{
    json d = load("mainnet_headers.json");
    CHECK_EQ(d["nfactor"].get<unsigned>(), kMainnetNFactor);
    ScryptHasher hasher(kMainnetNFactor);
    for (auto& v : d["vectors"]) {
        auto header = from_hex(v["header_hex"].get<std::string>());
        CHECK_EQ(header.size(), kHeaderSize);
        auto t0 = std::chrono::steady_clock::now();
        Hash256 h = hasher.hash(header.data(), header.size());
        double dt = seconds_since(t0);
        std::string got = to_hex_reversed(h.data(), h.size());
        CHECK_EQ(got, v["hash"].get<std::string>());
        // The block also meets its own target (plan F5).
        Hash256 target = target_from_compact(static_cast<uint32_t>(std::stoul(v["bits"].get<std::string>(), nullptr, 16)));
        CHECK(hash_meets_target(h, target));
        std::printf("    height %d: %s  (%.3f s, reused scratch)\n", v["height"].get<int>(), got.c_str(), dt);
    }
}

TEST(known_answer_testchain_nfactor4)
{
    // Blocks mined by the low-difficulty yacoind itself (generatetoaddress) at
    // N-factor 4: the node's hash, not just our own code compared with itself.
    json d = load("testchain_headers.json");
    CHECK_EQ(d["nfactor"].get<unsigned>(), 4u);
    ScryptHasher hasher(4);
    for (auto& v : d["vectors"]) {
        auto header = from_hex(v["header_hex"].get<std::string>());
        CHECK_EQ(header.size(), kHeaderSize);
        Hash256 h = hasher.hash(header.data(), header.size());
        CHECK_EQ(to_hex_reversed(h.data(), 32), v["hash"].get<std::string>());
        CHECK_EQ(BlockHeader::parse(header.data()).nonce, v["nonce"].get<uint32_t>());
    }
}

TEST(reference_allocating_hash_matches_scratch_hash)
{
    // The node's way (scrypt-jane allocates 512 MiB per call) gives the same
    // result as our reused buffer, also when huge pages are requested (this
    // checks the result only, not whether the kernel used huge pages).
    json d = load("mainnet_headers.json");
    auto& v = d["vectors"].back();
    auto header = from_hex(v["header_hex"].get<std::string>());
    auto t0 = std::chrono::steady_clock::now();
    Hash256 ref = pow_hash_reference(header.data(), header.size(), kMainnetNFactor);
    std::printf("    reference (allocating) hash: %.3f s\n", seconds_since(t0));
    CHECK_EQ(to_hex_reversed(ref.data(), 32), v["hash"].get<std::string>());

    ScryptHasher huge(kMainnetNFactor, true);
    CHECK(huge.hash(header.data(), header.size()) == ref);
}

TEST(small_nfactor_scratch_matches_reference)
{
    // Many inputs at the test chain's N-factor 4 and a few others.
    for (unsigned nf : {0u, 1u, 4u, 9u}) {
        ScryptHasher hasher(nf);
        for (int i = 0; i < 50; ++i) {
            uint8_t buf[kHeaderSize];
            for (size_t j = 0; j < kHeaderSize; ++j) buf[j] = static_cast<uint8_t>(i * 31 + j * 7 + nf);
            CHECK(hasher.hash(buf, sizeof buf) == pow_hash_reference(buf, sizeof buf, nf));
        }
    }
}

// ------------------------------------------------------------ lanes (T-09)

// Lane option sets every lane test runs with (T-10 adds the fused mix).
static std::vector<unsigned> lane_flag_sets()
{
    std::vector<unsigned> f = {kPrefetchT0, kPrefetchNta, kPrefetchNone};
    if (fused_mix_available())
        for (unsigned m : {unsigned(kMixFused2), unsigned(kMixFused4)})
            for (unsigned p : {unsigned(kPrefetchT0), unsigned(kPrefetchNone)}) f.push_back(m | p);
    return f;
}

static std::vector<std::vector<uint8_t>> vector_headers(const json& d)
{
    std::vector<std::vector<uint8_t>> v;
    for (auto& e : d["vectors"]) v.push_back(from_hex(e["header_hex"].get<std::string>()));
    return v;
}

static std::vector<std::string> vector_hashes(const json& d)
{
    std::vector<std::string> v;
    for (auto& e : d["vectors"]) v.push_back(e["hash"].get<std::string>());
    return v;
}

// Every lane position of every lane count 1..max_lanes gets each known-answer
// vector once (vector i goes to lane (i + shift) mod L, other lanes get
// filler headers).
static void check_lane_known_answers(const json& d, unsigned nfactor, unsigned max_lanes, unsigned flags)
{
    auto headers = vector_headers(d);
    auto hashes = vector_hashes(d);
    for (unsigned L = 1; L <= max_lanes; ++L) {
        ScryptHasher hasher(nfactor, false, L, flags);
        for (unsigned shift = 0; shift < L; ++shift) {
            std::vector<std::vector<uint8_t>> in(L, std::vector<uint8_t>(kHeaderSize));
            std::vector<int> which(L, -1);
            for (unsigned k = 0; k < L; ++k)
                for (size_t j = 0; j < kHeaderSize; ++j) in[k][j] = static_cast<uint8_t>(k * 17 + j * 3 + shift);
            for (size_t i = 0; i < headers.size(); ++i) {
                unsigned k = static_cast<unsigned>((i + shift) % L);
                in[k] = headers[i];
                which[k] = static_cast<int>(i);
                std::vector<const uint8_t*> ptr(L);
                for (unsigned q = 0; q < L; ++q) ptr[q] = in[q].data();
                std::vector<Hash256> out(L);
                hasher.hash_lanes(ptr.data(), kHeaderSize, out.data(), L);
                CHECK_EQ(to_hex_reversed(out[k].data(), 32), hashes[i]);
            }
        }
    }
}

TEST(lanes_known_answer_testchain_nfactor4_every_lane)
{
    json d = load("testchain_headers.json");
    for (unsigned flags : lane_flag_sets()) check_lane_known_answers(d, 4, kMaxLanes, flags);
}

TEST(lanes_known_answer_mainnet_nfactor21_every_lane)
{
    // L = 2..4. Mainnet vector i goes to lane i mod L, so each lane position of each L
    // is checked against a real mainnet block hash.
    json d = load("mainnet_headers.json");
    auto headers = vector_headers(d);
    auto hashes = vector_hashes(d);
    for (unsigned L = 2; L <= 4; ++L) {
        ScryptHasher hasher(kMainnetNFactor, true, L);
        std::vector<const uint8_t*> ptr(L);
        std::vector<size_t> idx(L);
        for (unsigned k = 0; k < L; ++k) {
            idx[k] = (k + L) % headers.size();  // different vectors per L
            ptr[k] = headers[idx[k]].data();
        }
        std::vector<Hash256> out(L);
        auto t0 = std::chrono::steady_clock::now();
        hasher.hash_lanes(ptr.data(), kHeaderSize, out.data(), L);
        double dt = seconds_since(t0);
        for (unsigned k = 0; k < L; ++k) CHECK_EQ(to_hex_reversed(out[k].data(), 32), hashes[idx[k]]);
        std::printf("    %u lanes at N-factor 21: %.2f s (%.2f H/s, 1 thread)\n", L, dt, L / dt);
    }
}

// A small deterministic random generator (tests must be reproducible).
struct TestRng {
    uint64_t s;
    uint32_t next()
    {
        s = s * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<uint32_t>(s >> 33);
    }
};

TEST(lanes_differential_random_headers_nfactor4)
{
    // A different random header and nonce per lane, random lane counts and
    // partial batches (count < lanes), against the reference hash.
    TestRng rng{0x5eed};
    for (unsigned flags : lane_flag_sets()) {
        for (unsigned L = 1; L <= kMaxLanes; ++L) {
            ScryptHasher hasher(4, false, L, flags);
            for (int iter = 0; iter < 25; ++iter) {
                unsigned count = 1 + rng.next() % L;
                uint8_t in[kMaxLanes][kHeaderSize];
                const uint8_t* ptr[kMaxLanes];
                for (unsigned k = 0; k < count; ++k) {
                    for (size_t j = 0; j < kHeaderSize; ++j) in[k][j] = static_cast<uint8_t>(rng.next());
                    set_header_nonce(in[k], rng.next());
                    ptr[k] = in[k];
                }
                Hash256 out[kMaxLanes];
                hasher.hash_lanes(ptr, kHeaderSize, out, count);
                for (unsigned k = 0; k < count; ++k) CHECK(out[k] == pow_hash_reference(in[k], kHeaderSize, 4));
            }
        }
    }
}

TEST(lanes_differential_random_headers_nfactor21)
{
    // Different random headers per lane at N-factor 21 (the production
    // setting) with each mix: L = 5 puts the last table 2 GiB + 8 MiB into
    // one allocation (past 2^31: catches a signed 32-bit offset), and with
    // fused4 it runs one quad plus one single lane; fused2 at L = 4 runs two
    // pairs.
    TestRng rng{21};
    std::vector<std::pair<unsigned, unsigned>> cases = {{5, kPrefetchT0}};
    if (fused_mix_available()) {
        cases.push_back({4, kMixFused2 | kPrefetchT0});
        cases.push_back({5, kMixFused4 | kPrefetchT0});
    }
    ScryptHasher one(kMainnetNFactor, true);  // reference, reused (plain path)
    for (auto [L, flags] : cases) {
        ScryptHasher hasher(kMainnetNFactor, true, L, flags);
        uint8_t in[kMaxLanes][kHeaderSize];
        const uint8_t* ptr[kMaxLanes];
        for (unsigned k = 0; k < L; ++k) {
            for (size_t j = 0; j < kHeaderSize; ++j) in[k][j] = static_cast<uint8_t>(rng.next());
            ptr[k] = in[k];
        }
        Hash256 out[kMaxLanes];
        auto t0 = std::chrono::steady_clock::now();
        hasher.hash_lanes(ptr, kHeaderSize, out, L);
        std::printf("    %u lanes, flags %u at N-factor 21: %.2f s\n", L, flags, seconds_since(t0));
        for (unsigned k = 0; k < L; ++k) CHECK(out[k] == one.hash(in[k], kHeaderSize));
    }
}

TEST(fused_chunkmix_is_bit_identical)
{
    // T-10: the fused 2-lane and 4-lane AVX2 ChunkMix against scrypt-jane's
    // own ChunkMix (yac_scrypt_chunkmix) called once per lane, on random
    // chunks, with and without the xor input.
    if (!fused_mix_available()) {
        std::printf("    fused mix not compiled in (no AVX2 build); skipped\n");
        return;
    }
    TestRng rng{0xc4ac4a};
    alignas(64) uint32_t in[4][32], xr[4][32], ref[4][32], got[4][32];
    for (int iter = 0; iter < 2000; ++iter) {
        for (int k = 0; k < 4; ++k)
            for (int w = 0; w < 32; ++w) {
                in[k][w] = rng.next();
                xr[k][w] = rng.next();
            }
        const bool use_xor = iter % 2;
        for (int k = 0; k < 4; ++k) yac_scrypt_chunkmix(ref[k], in[k], use_xor ? xr[k] : nullptr);
        std::memset(got, 0, sizeof got);
        yac_scrypt_chunkmix2(got[0], in[0], use_xor ? xr[0] : nullptr, got[1], in[1], use_xor ? xr[1] : nullptr);
        CHECK(std::memcmp(got[0], ref[0], 128) == 0);
        CHECK(std::memcmp(got[1], ref[1], 128) == 0);
        std::memset(got, 0, sizeof got);
        uint32_t* o[4] = {got[0], got[1], got[2], got[3]};
        uint32_t* i4[4] = {in[0], in[1], in[2], in[3]};
        uint32_t* x4[4] = {xr[0], xr[1], xr[2], xr[3]};
        yac_scrypt_chunkmix4(o, i4, use_xor ? x4 : nullptr);
        CHECK(std::memcmp(got, ref, sizeof ref) == 0);
    }
}

TEST(lanes_reject_bad_counts)
{
    CHECK_THROWS(ScryptHasher(4, false, 0));
    CHECK_THROWS(ScryptHasher(4, false, kMaxLanes + 1));
    ScryptHasher h(4, false, 2);
    uint8_t buf[kHeaderSize] = {};
    const uint8_t* ptr[3] = {buf, buf, buf};
    Hash256 out[3];
    CHECK_THROWS(h.hash_lanes(ptr, kHeaderSize, out, 3));
    CHECK_THROWS(h.hash_lanes(ptr, kHeaderSize, out, 0));
}

TEST(micro_benchmark_nfactor21_one_thread)
{
    // Input for T-06: hashes per second for one thread with a reused buffer.
    ScryptHasher hasher(kMainnetNFactor);
    uint8_t buf[kHeaderSize] = {7};
    const int n = 4;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
        set_header_nonce(buf, static_cast<uint32_t>(i));
        hasher.hash(buf, sizeof buf);
    }
    double dt = seconds_since(t0);
    std::printf("    micro-benchmark: %d hashes at N-factor 21 in %.2f s = %.2f H/s (1 thread, %s)\n", n, dt,
                n / dt, scrypt_variant().c_str());
    CHECK(dt > 0);
}

TEST_MAIN()
