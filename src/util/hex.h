// yacoin-cpuminer: hex helpers. MIT licence.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace yac {

// Lower-case hex of the bytes, in memory order.
std::string to_hex(const uint8_t* data, size_t len);
std::string to_hex(const std::vector<uint8_t>& v);

// Parses hex (upper or lower case, even length). Throws std::invalid_argument.
std::vector<uint8_t> from_hex(const std::string& hex);

// Hex of a 256-bit little-endian number as the node prints a uint256
// (most significant byte first, i.e. bytes reversed).
std::string to_hex_reversed(const uint8_t* data, size_t len);

}  // namespace yac
