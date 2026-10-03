// yacoin-cpuminer: the packed version-7 block header. MIT licence.
#include "work/header.h"

#include <cstring>

#include "util/hex.h"

namespace yac {

static void put_le(uint8_t* p, uint64_t v, int n)
{
    for (int i = 0; i < n; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

static uint64_t get_le(const uint8_t* p, int n)
{
    uint64_t v = 0;
    for (int i = 0; i < n; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
    return v;
}

std::array<uint8_t, kHeaderSize> BlockHeader::serialize() const
{
    std::array<uint8_t, kHeaderSize> b{};
    put_le(&b[0], version, 4);
    std::memcpy(&b[4], prev_block.data(), 32);
    std::memcpy(&b[36], merkle_root.data(), 32);
    put_le(&b[68], static_cast<uint64_t>(time), 8);
    put_le(&b[76], bits, 4);
    put_le(&b[80], nonce, 4);
    return b;
}

BlockHeader BlockHeader::parse(const uint8_t* b)
{
    BlockHeader h;
    h.version = static_cast<uint32_t>(get_le(&b[0], 4));
    std::memcpy(h.prev_block.data(), &b[4], 32);
    std::memcpy(h.merkle_root.data(), &b[36], 32);
    h.time = static_cast<int64_t>(get_le(&b[68], 8));
    h.bits = static_cast<uint32_t>(get_le(&b[76], 4));
    h.nonce = static_cast<uint32_t>(get_le(&b[80], 4));
    return h;
}

std::string BlockHeader::prev_hex() const { return to_hex_reversed(prev_block.data(), 32); }
std::string BlockHeader::merkle_hex() const { return to_hex_reversed(merkle_root.data(), 32); }

}  // namespace yac
