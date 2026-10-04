// yacoin-cpuminer: Yacoin proof-of-work hash. MIT licence.
#include "hash/pow_hash.h"

#include <sys/mman.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

#include "hash/yac_scrypt.h"

namespace yac {

bool scrypt_self_test() { return yac_scrypt_self_test() == 1; }

std::string scrypt_variant() { return yac_scrypt_mix_name(); }

bool fused_mix_available() { return yac_scrypt_chunkmix2 != nullptr; }

size_t scratch_bytes(unsigned nfactor)
{
    size_t b = yac_scrypt_scratch_bytes(nfactor);
    if (b == 0) throw std::invalid_argument("N-factor out of range");
    return b;
}

Hash256 pow_hash_reference(const uint8_t* data, size_t len, unsigned nfactor)
{
    if (nfactor > YAC_SCRYPT_MAX_NFACTOR) throw std::invalid_argument("N-factor out of range");
    Hash256 out{};
    // Same call as yacoin's scrypt_hash: salt = input, rfactor = pfactor = 0.
    if (scrypt(data, len, data, len, static_cast<unsigned char>(nfactor), 0, 0, out.data(), out.size()) != 1)
        throw std::runtime_error("scrypt failed (out of memory?)");
    return out;
}

static constexpr size_t kHugePage = 2u << 20;

ScryptHasher::ScryptHasher(unsigned nfactor, bool huge_pages, unsigned lanes, unsigned lane_flags)
    : nfactor_(nfactor), bytes_(scratch_bytes(nfactor)), huge_(huge_pages), lanes_(lanes), flags_(lane_flags)
{
    if (lanes_ < 1 || lanes_ > kMaxLanes) throw std::invalid_argument("lanes must be 1.." + std::to_string(kMaxLanes));
    size_t align = huge_ ? kHugePage : 4096;
    stride_ = (bytes_ + align - 1) & ~(align - 1);  // each table starts aligned (>= 128 B)
    map_len_ = stride_ * lanes_ + align;
    map_ = mmap(nullptr, map_len_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (map_ == MAP_FAILED) {
        map_ = nullptr;
        throw std::runtime_error(std::string("cannot allocate scrypt scratch memory: ") + std::strerror(errno));
    }
    uintptr_t p = reinterpret_cast<uintptr_t>(map_);
    p = (p + align - 1) & ~(uintptr_t)(align - 1);
    scratch_ = reinterpret_cast<uint8_t*>(p);
#ifdef MADV_HUGEPAGE
    if (huge_) madvise(map_, map_len_, MADV_HUGEPAGE);  // a hint; failure is harmless
#endif
}

ScryptHasher::~ScryptHasher()
{
    if (map_) munmap(map_, map_len_);
}

void ScryptHasher::hash(const uint8_t* data, size_t len, Hash256& out)
{
    if (yac_scrypt_hash_scratch(data, len, nfactor_, scratch_, out.data()) != 1)
        throw std::runtime_error("scrypt hash failed (bad arguments)");
}

void ScryptHasher::hash_lanes(const uint8_t* const* inputs, size_t len, Hash256* outs, unsigned count)
{
    if (count < 1 || count > lanes_) throw std::invalid_argument("hash_lanes: bad lane count");
    if (count == 1) {
        hash(inputs[0], len, outs[0]);
        return;
    }
    uint8_t* tables[kMaxLanes];
    uint8_t out[kMaxLanes][32];
    for (unsigned k = 0; k < count; ++k) tables[k] = table(k);
    if (yac_scrypt_hash_lanes(inputs, len, nfactor_, tables, out, count, flags_) != 1)
        throw std::runtime_error("scrypt lane hash failed (bad arguments)");
    for (unsigned k = 0; k < count; ++k) std::memcpy(outs[k].data(), out[k], 32);
}

}  // namespace yac
