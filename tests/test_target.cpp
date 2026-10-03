// yacoin-cpuminer: target conversion and comparison (plan §7.3, F5). MIT licence.
#include "testing.h"

#include "util/hex.h"
#include "work/target.h"

using namespace yac;

static std::string be(const Hash256& h) { return to_hex_reversed(h.data(), h.size()); }

TEST(mainnet_min_difficulty_target)
{
    // 1e0fffff -> 00000fffff000000... (plan §4; powLimit ~uint256(0) >> 20 compacted)
    CHECK_EQ(be(target_from_compact(0x1e0fffff)),
             std::string("00000fffff000000000000000000000000000000000000000000000000000000"));
    // ~1,048,577 hashes per block on average (plan §4)
    double e = expected_hashes(target_from_compact(0x1e0fffff));
    CHECK(e > 1048576.0 && e < 1048578.0);
}

TEST(testchain_initial_target)
{
    // Low-difficulty build: nBits 2000ffff, getwork target ...00ffff00 (LE).
    Hash256 t = target_from_compact(0x2000ffff);
    CHECK_EQ(to_hex(t.data(), 32), std::string("0000000000000000000000000000000000000000000000000000000000ffff00"));
}

TEST(small_sizes_and_errors)
{
    CHECK_EQ(be(target_from_compact(0x03123456)).substr(58), std::string("123456"));
    CHECK_EQ(be(target_from_compact(0x02123456)).substr(60), std::string("1234"));
    CHECK_EQ(be(target_from_compact(0x01123456)).substr(62), std::string("12"));
    CHECK_THROWS(target_from_compact(0x1e8fffff));  // sign bit
    CHECK_THROWS(target_from_compact(0x22123456));  // overflow
}

TEST(compare_hash_to_target)
{
    Hash256 t = target_from_compact(0x1e0fffff);
    Hash256 h = t;
    CHECK(hash_meets_target(h, t));  // equal is valid (node fails only if hash > target)
    h[0] = 1;                        // t[0] == 0 -> hash now larger by 1
    CHECK(!hash_meets_target(h, t));
    Hash256 low{};
    low[29] = 0x0f;
    CHECK(hash_meets_target(low, t));
    Hash256 high{};
    high[29] = 0x10;  // 0x000010... > 0x00000fffff...
    CHECK(!hash_meets_target(high, t));
    Hash256 top{};
    top[31] = 1;
    CHECK(!hash_meets_target(top, t));
}

TEST_MAIN()
