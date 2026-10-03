// yacoin-cpuminer: getwork data decoding and encoding (plan F6, F7, F10). MIT licence.
#pragma once

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "hash/pow_hash.h"
#include "work/header.h"

namespace yac {

constexpr size_t kGetworkDataSize = 128;

// Thrown for work this miner cannot mine (e.g. version < 7).
struct UnsupportedWork : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// One unit of work from `getwork` (no arguments).
struct Work {
    // `data` with the per-word byte reversal undone: the 84-byte header,
    // then SHA-256 padding (0x80, zeros, bit length 672 big-endian).
    std::array<uint8_t, kGetworkDataSize> plain{};
    BlockHeader header;  // parsed from plain[0..83]
    Hash256 target{};    // raw little-endian uint256 from `target`

    const uint8_t* header_bytes() const { return plain.data(); }
};

// Reverses the bytes of every 32-bit word in place (the node's ByteReverse
// loop in FormatHashBuffers_64bit_nTime and in the getwork submit branch).
void reverse_words(uint8_t* buf, size_t len);

// Decodes `getwork` `data` (256 hex chars) and `target` (64 hex chars).
// Checks the size and padding, and throws UnsupportedWork for a block version
// below 7 (80-byte header with date-based N-factor) and std::invalid_argument
// for malformed data.
Work decode_getwork(const std::string& data_hex, const std::string& target_hex);

// Builds the `getwork <data>` argument for a solution: the node's 128 bytes
// with only the nonce replaced, word-reversed again. The timestamp and every
// other byte stay exactly as the node sent them (plan F10); the node reads
// back only timestamp and nonce, and uses merkle_root to find its saved block
// (plan F7).
std::string encode_getwork_submit(const Work& work, uint32_t nonce);

}  // namespace yac
