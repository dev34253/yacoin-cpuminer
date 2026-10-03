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

}  // namespace yac
