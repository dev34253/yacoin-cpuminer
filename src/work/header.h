// yacoin-cpuminer: the packed version-7 block header (plan F1). MIT licence.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "hash/pow_hash.h"

namespace yac {

// First block version with a 64-bit nTime and the 84-byte header
// (VERSION_of_block_for_yac_05x_new in yacoin). Older versions use an 80-byte
// header and a date-based N-factor; this miner does not support them.
constexpr uint32_t kMinBlockVersion = 7;

// Header fields. Hashes are kept in their serialized (little-endian) byte
// order; the node prints them reversed (see to_hex_reversed).
struct BlockHeader {
    uint32_t version = 0;
    Hash256 prev_block{};
    Hash256 merkle_root{};
    int64_t time = 0;
    uint32_t bits = 0;
    uint32_t nonce = 0;

    // The 84 bytes the node hashes and serializes (`struct block_header`,
    // packed, little-endian): version 0..3, prev 4..35, merkle 36..67,
    // time 68..75, bits 76..79, nonce 80..83.
    std::array<uint8_t, kHeaderSize> serialize() const;
    static BlockHeader parse(const uint8_t* bytes84);

    std::string prev_hex() const;    // as the node prints it (getbestblockhash)
    std::string merkle_hex() const;  // as the node prints it
};

constexpr size_t kNonceOffset = 80;

// Writes a nonce into serialized header bytes (little-endian at offset 80).
inline void set_header_nonce(uint8_t* bytes84, uint32_t nonce)
{
    bytes84[kNonceOffset + 0] = static_cast<uint8_t>(nonce);
    bytes84[kNonceOffset + 1] = static_cast<uint8_t>(nonce >> 8);
    bytes84[kNonceOffset + 2] = static_cast<uint8_t>(nonce >> 16);
    bytes84[kNonceOffset + 3] = static_cast<uint8_t>(nonce >> 24);
}

}  // namespace yac
