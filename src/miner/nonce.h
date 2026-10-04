// yacoin-cpuminer: nonce slicing. MIT licence.
#pragma once

#include <cstdint>

namespace yac {

// Half-open range [begin, end) of 32-bit nonces; end may be 2^32.
struct NonceRange {
    uint64_t begin;
    uint64_t end;
    uint64_t size() const { return end - begin; }
};

// Thread `index` of `count` scans its own slice of the 32-bit nonce space.
// The slices are disjoint and together cover 0 .. 2^32-1.
inline NonceRange nonce_slice(unsigned index, unsigned count)
{
    const uint64_t space = uint64_t(1) << 32;
    return {space * index / count, space * (index + 1) / count};
}

// One call of ScryptHasher::hash_lanes: `count` consecutive nonces starting
// at `first`; lane k hashes nonce first + k (T-09).
struct LaneBatch {
    uint64_t first;
    unsigned count;
    uint32_t nonce(unsigned lane) const { return static_cast<uint32_t>(first + lane); }
};

// The batch that starts at `next` inside `range`: `lanes` nonces, or fewer
// for the last, partial batch of the slice; count 0 when the slice is done.
inline LaneBatch next_batch(uint64_t next, const NonceRange& range, unsigned lanes)
{
    if (next >= range.end) return {next, 0};
    uint64_t left = range.end - next;
    return {next, static_cast<unsigned>(left < lanes ? left : lanes)};
}

}  // namespace yac
