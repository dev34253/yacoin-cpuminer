// yacoin-cpuminer: tests for small utilities. MIT licence.
#include "testing.h"

#include "hash/yac_scrypt.h"
#include "util/hex.h"

using namespace yac;

TEST(hex_round_trip)
{
    std::vector<uint8_t> v = {0x00, 0x01, 0xab, 0xff};
    CHECK_EQ(to_hex(v), std::string("0001abff"));
    CHECK(from_hex("0001ABff") == v);
    CHECK_EQ(to_hex_reversed(v.data(), v.size()), std::string("ffab0100"));
}

TEST(hex_rejects_bad_input)
{
    CHECK_THROWS(from_hex("abc"));
    CHECK_THROWS(from_hex("zz"));
}

TEST(scrypt_jane_self_test_passes)
{
    CHECK(yac_scrypt_self_test() == 1);
    CHECK(std::string(yac_scrypt_mix_name()).find("ChaCha") != std::string::npos);
}

TEST_MAIN()
