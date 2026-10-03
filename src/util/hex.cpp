// yacoin-cpuminer: hex helpers. MIT licence.
#include "util/hex.h"

#include <stdexcept>

namespace yac {

static const char kDigits[] = "0123456789abcdef";

std::string to_hex(const uint8_t* data, size_t len)
{
    std::string s;
    s.reserve(len * 2);
    for (size_t i = 0; i < len; ++i) {
        s.push_back(kDigits[data[i] >> 4]);
        s.push_back(kDigits[data[i] & 0xf]);
    }
    return s;
}

std::string to_hex(const std::vector<uint8_t>& v) { return to_hex(v.data(), v.size()); }

std::string to_hex_reversed(const uint8_t* data, size_t len)
{
    std::string s;
    s.reserve(len * 2);
    for (size_t i = len; i-- > 0;) {
        s.push_back(kDigits[data[i] >> 4]);
        s.push_back(kDigits[data[i] & 0xf]);
    }
    return s;
}

static int nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::vector<uint8_t> from_hex(const std::string& hex)
{
    if (hex.size() % 2 != 0) throw std::invalid_argument("hex string has odd length");
    std::vector<uint8_t> out(hex.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        int hi = nibble(hex[2 * i]), lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) throw std::invalid_argument("invalid hex digit");
        out[i] = static_cast<uint8_t>(hi << 4 | lo);
    }
    return out;
}

}  // namespace yac
