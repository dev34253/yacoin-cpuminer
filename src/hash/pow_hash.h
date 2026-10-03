// yacoin-cpuminer: Yacoin proof-of-work hash (plan F1-F4). MIT licence.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace yac {

using Hash256 = std::array<uint8_t, 32>;  // little-endian uint256, as the node stores it

// Size of the packed version-7 header that is hashed (plan F1).
constexpr size_t kHeaderSize = 84;
// Mainnet N-factor (node default -nFactorAtHardfork=21, plan F2).
constexpr unsigned kMainnetNFactor = 21;

// Runs scrypt-jane's self-test once. Call from main before any worker thread
// starts (the test is not thread-safe, plan F4). A failure exits the process
// with code 21 after printing the reason.
bool scrypt_self_test();

// Name of the compiled ChaCha variant ("ChaCha/8-AVX", ...).
std::string scrypt_variant();

// Bytes of scratch memory one hash needs at this N-factor (r = p = 1).
size_t scratch_bytes(unsigned nfactor);

// The node's way: scrypt-jane allocates and frees its memory on every call
// (yacoin src/scrypt.cpp scrypt_hash). Used as the reference in tests; call
// scrypt_self_test() first.
Hash256 pow_hash_reference(const uint8_t* data, size_t len, unsigned nfactor);

// A per-thread scratch buffer, allocated once (mmap, 64-byte aligned; with
// huge_pages the region is 2 MiB aligned and madvise(MADV_HUGEPAGE) is asked
// for). Not copyable; one per thread.
class ScryptHasher {
public:
    ScryptHasher(unsigned nfactor, bool huge_pages = false);
    ~ScryptHasher();
    ScryptHasher(const ScryptHasher&) = delete;
    ScryptHasher& operator=(const ScryptHasher&) = delete;

    // PoW hash of `len` bytes (84 for a version-7 header).
    void hash(const uint8_t* data, size_t len, Hash256& out);
    Hash256 hash(const uint8_t* data, size_t len)
    {
        Hash256 h;
        hash(data, len, h);
        return h;
    }

    unsigned nfactor() const { return nfactor_; }
    size_t bytes() const { return bytes_; }
    bool huge_pages_requested() const { return huge_; }

private:
    unsigned nfactor_;
    size_t bytes_;
    bool huge_;
    void* map_ = nullptr;
    size_t map_len_ = 0;
    uint8_t* scratch_ = nullptr;
};

}  // namespace yac
