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

// True if a fused multi-lane ChunkMix is compiled in (AVX2 build, T-10);
// otherwise kMixFused2/kMixFused4 fall back to the plain per-lane mix.
bool fused_mix_available();

// Bytes of scratch memory one hash needs at this N-factor (r = p = 1).
size_t scratch_bytes(unsigned nfactor);

// The node's way: scrypt-jane allocates and frees its memory on every call
// (yacoin src/scrypt.cpp scrypt_hash). Used as the reference in tests; call
// scrypt_self_test() first.
Hash256 pow_hash_reference(const uint8_t* data, size_t len, unsigned nfactor);

// Most hashes one ScryptHasher computes per call (lanes, plan §12).
constexpr unsigned kMaxLanes = 8;

// Lane options (bits of yac_scrypt_hash_lanes' flags).
enum LaneFlags : unsigned {
    kPrefetchNone = 0,
    kPrefetchT0 = 1,
    kPrefetchNta = 2,
    kMixFused2 = 4,
    kMixFused4 = 8,
};
constexpr unsigned kDefaultLaneFlags = kPrefetchT0;

// A per-thread scratch buffer for `lanes` tables, allocated once (one mmap;
// every table starts on a 2 MiB boundary with huge_pages, else on a 4 KiB
// one, and madvise(MADV_HUGEPAGE) is asked for with huge_pages). Not
// copyable; one per thread.
class ScryptHasher {
public:
    ScryptHasher(unsigned nfactor, bool huge_pages = false, unsigned lanes = 1,
                 unsigned lane_flags = kDefaultLaneFlags);
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

    // PoW hashes of `count` (1..lanes()) inputs of `len` bytes each, computed
    // together (T-09 lanes). count == 1 uses the plain scrypt-jane path, the
    // same as hash().
    void hash_lanes(const uint8_t* const* inputs, size_t len, Hash256* outs, unsigned count);

    unsigned nfactor() const { return nfactor_; }
    size_t bytes() const { return bytes_; }
    bool huge_pages_requested() const { return huge_; }
    unsigned lanes() const { return lanes_; }
    uint8_t* table(unsigned lane) const { return scratch_ + lane * stride_; }

private:
    unsigned nfactor_;
    size_t bytes_;
    bool huge_;
    unsigned lanes_;
    unsigned flags_;
    size_t stride_ = 0;  // bytes from one lane's table to the next
    void* map_ = nullptr;
    size_t map_len_ = 0;
    uint8_t* scratch_ = nullptr;
};

}  // namespace yac
