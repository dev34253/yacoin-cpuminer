// yacoin-cpuminer: PoW target maths (plan F5). MIT licence.
#pragma once

#include <cstdint>

#include "hash/pow_hash.h"

namespace yac {

// Compact nBits -> 256-bit little-endian target, like CBigNum::SetCompact in
// the node. Throws std::invalid_argument for a negative or overflowing value.
Hash256 target_from_compact(uint32_t bits);

// The node's PoW check: hash <= target, both read as 256-bit little-endian
// numbers and compared from the most significant byte down
// (yacoin src/miner.cpp CheckWork: `hashBlock > hashTarget` fails).
bool hash_meets_target(const Hash256& hash, const Hash256& target);

// Expected number of hashes per block: 2^256 / (target + 1), as a double.
double expected_hashes(const Hash256& target);

}  // namespace yac
