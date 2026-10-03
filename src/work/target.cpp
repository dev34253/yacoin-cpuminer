// yacoin-cpuminer: PoW target maths. MIT licence.
#include "work/target.h"

#include <cmath>
#include <stdexcept>

namespace yac {

Hash256 target_from_compact(uint32_t bits)
{
    Hash256 t{};
    unsigned size = bits >> 24;
    uint32_t word = bits & 0x007fffff;
    // The node would read a set sign bit as a negative number (and size 0 as
    // zero); neither is a valid nBits for a block, so refuse them here.
    if (bits & 0x00800000) throw std::invalid_argument("negative compact target");
    if (size <= 3) {
        word >>= 8 * (3 - size);
        for (unsigned i = 0; i < 3; ++i) t[i] = static_cast<uint8_t>(word >> (8 * i));
    } else {
        // mantissa bytes go to positions size-3 .. size-1
        for (unsigned i = 0; i < 3; ++i) {
            uint8_t byte = static_cast<uint8_t>(word >> (8 * i));
            unsigned pos = size - 3 + i;
            if (pos >= 32) {
                if (byte != 0) throw std::invalid_argument("compact target overflows 256 bits");
                continue;
            }
            t[pos] = byte;
        }
    }
    return t;
}

bool hash_meets_target(const Hash256& hash, const Hash256& target)
{
    for (int i = 31; i >= 0; --i) {
        if (hash[i] < target[i]) return true;
        if (hash[i] > target[i]) return false;
    }
    return true;  // equal
}

double expected_hashes(const Hash256& target)
{
    double t = 0;
    for (int i = 31; i >= 0; --i) t = t * 256.0 + target[i];
    return std::ldexp(1.0, 256) / (t + 1.0);
}

}  // namespace yac
