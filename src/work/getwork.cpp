// yacoin-cpuminer: getwork data decoding and encoding. MIT licence.
#include "work/getwork.h"

#include <utility>

#include "util/hex.h"

namespace yac {

void reverse_words(uint8_t* buf, size_t len)
{
    for (size_t i = 0; i + 4 <= len; i += 4) {
        std::swap(buf[i], buf[i + 3]);
        std::swap(buf[i + 1], buf[i + 2]);
    }
}

Work decode_getwork(const std::string& data_hex, const std::string& target_hex)
{
    auto data = from_hex(data_hex);
    if (data.size() != kGetworkDataSize)
        throw std::invalid_argument("getwork data must be 128 bytes, got " + std::to_string(data.size()));
    auto target = from_hex(target_hex);
    if (target.size() != 32)
        throw std::invalid_argument("getwork target must be 32 bytes, got " + std::to_string(target.size()));

    Work w;
    std::copy(data.begin(), data.end(), w.plain.begin());
    reverse_words(w.plain.data(), w.plain.size());

    // Version first: a version-6 block has an 80-byte header and different padding.
    w.header = BlockHeader::parse(w.plain.data());
    if (w.header.version < kMinBlockVersion)
        throw UnsupportedWork("block version " + std::to_string(w.header.version) +
                              " is not supported (needs >= 7: 84-byte header, 64-bit time). "
                              "On a test chain, start the node with -testnetNewLogicBlockNumber=0.");

    // SHA-256 padding of an 84-byte message: 0x80, zeros, length 672 bits.
    bool pad_ok = w.plain[84] == 0x80 && w.plain[126] == 0x02 && w.plain[127] == 0xa0;
    for (size_t i = 85; i < 126 && pad_ok; ++i) pad_ok = w.plain[i] == 0;
    if (!pad_ok) throw std::invalid_argument("getwork data has unexpected padding (not an 84-byte header?)");

    std::copy(target.begin(), target.end(), w.target.begin());
    return w;
}

std::string encode_getwork_submit(const Work& work, uint32_t nonce)
{
    std::array<uint8_t, kGetworkDataSize> out = work.plain;
    set_header_nonce(out.data(), nonce);
    reverse_words(out.data(), out.size());
    return to_hex(out.data(), out.size());
}

}  // namespace yac
