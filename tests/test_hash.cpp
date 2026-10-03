// yacoin-cpuminer: known-answer tests for the PoW hash (plan §7.1, task T-02).
//
// tests/data/mainnet_headers.json holds real mainnet version-7 headers taken
// with read-only RPC (`getblockheader <hash> false`): the first block after
// the 1,890,000 fork, the fork block itself, two in between and the snapshot
// tip 1,964,617. For each one, the scrypt hash at N-factor 21 of the header
// rebuilt from its fields must equal the block hash. This proves F1-F3.
#include <chrono>
#include <cstdio>
#include <fstream>

#include "testing.h"

#include "hash/pow_hash.h"
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
