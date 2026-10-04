/* yacoin-cpuminer: C entry points around the copied scrypt-jane. MIT licence. */
#ifndef YAC_SCRYPT_H
#define YAC_SCRYPT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* scrypt-jane's own limit (scrypt_maxNfactor). */
#define YAC_SCRYPT_MAX_NFACTOR 30

/* Runs scrypt-jane's power-on self-test (mix, hash, scrypt vectors) through a
 * first scrypt() call, which also sets scrypt-jane's internal "already tested"
 * flag. Not thread-safe: call once from main before any thread hashes. A
 * failing self-test prints the reason and calls exit(21) (scrypt-jane's
 * default fatal-error handler); returns 1 otherwise. */
int yac_scrypt_self_test(void);

/* Name of the ChaCha mix variant compiled in, e.g. "ChaCha/8-AVX". */
const char *yac_scrypt_mix_name(void);

/* Scratch bytes needed by yac_scrypt_hash_scratch for this N-factor with
 * r = p = 1: (2^(nfactor+1) + 2) * 128. 512 MiB + 256 B at N-factor 21,
 * 4 KiB + 256 B at N-factor 4. Returns 0 if nfactor is out of range. */
size_t yac_scrypt_scratch_bytes(unsigned nfactor);

/* Yacoin PoW hash, exactly as the node's scrypt_hash(input, len, out, Nfactor):
 * scrypt-jane with Keccak-512 + ChaCha20/8, salt = input, r = p = 1, 32-byte
 * output. `scratch` must hold yac_scrypt_scratch_bytes(nfactor) bytes, aligned
 * to 64 bytes; it is not cleared. Thread-safe as long as each thread uses its
 * own scratch and yac_scrypt_self_test() ran first. Returns 1 on success,
 * 0 on bad arguments. */
int yac_scrypt_hash_scratch(const uint8_t *input, size_t input_len, unsigned nfactor,
                            uint8_t *scratch, uint8_t out[32]);

/* ---- Several hashes per call (lanes, T-09/T-10, plan §12) ---- */

#define YAC_SCRYPT_MAX_LANES 8

/* Flags for yac_scrypt_hash_lanes. */
#define YAC_PREFETCH_NONE 0u
#define YAC_PREFETCH_T0 1u   /* prefetcht0 both lines of the next chunk (default) */
#define YAC_PREFETCH_NTA 2u  /* prefetchnta */
#define YAC_PREFETCH_MASK 3u
#define YAC_MIX_FUSED2 4u    /* fused 2-lane ChunkMix for lane pairs, if compiled in (T-10) */
#define YAC_MIX_FUSED4 8u    /* fused 4-lane ChunkMix (two pairs) for lane quads, then pairs */

/* The same PoW hash as yac_scrypt_hash_scratch for `lanes` (1..8) inputs at
 * once, each with its own table: scratch[k] must hold
 * yac_scrypt_scratch_bytes(nfactor) bytes, aligned to 128 bytes, and must not
 * overlap another lane's. out[k] receives the hash of inputs[k] (input_len
 * bytes each). The lanes take turns chunk by chunk, and each prefetches its
 * next random chunk (flags & YAC_PREFETCH_MASK) while the other lanes (or,
 * with a fused mix, the other lane groups) mix. With a fused mix and lanes
 * equal to the group size there is no other group, so the prefetch gets no
 * lead time: use lanes = 2 x group for that.
 * Returns 1 on success, 0 on bad arguments. */
int yac_scrypt_hash_lanes(const uint8_t *const *inputs, size_t input_len, unsigned nfactor,
                          uint8_t *const *scratch, uint8_t (*out)[32], unsigned lanes, unsigned flags);

/* One scrypt-jane ChunkMix with r = 1: out = H(in ^ xor_in) over 128-byte
 * chunks (xor_in may be NULL), with the compiled-in mix variant. For tests. */
void yac_scrypt_chunkmix(uint32_t *out, uint32_t *in, uint32_t *xor_in);

/* Fused 2-lane ChunkMix (T-10): the same as two yac_scrypt_chunkmix calls,
 * (out_a, in_a, xor_a) and (out_b, in_b, xor_b); xor_a and xor_b are both
 * NULL or both set. Chunks 16-byte aligned. */
typedef void (*yac_chunkmix2_fn)(uint32_t *out_a, uint32_t *in_a, uint32_t *xor_a,
                                 uint32_t *out_b, uint32_t *in_b, uint32_t *xor_b);
/* Two fused pairs: lanes 0-3 of out/in/xor_in (xor_in NULL or 4 pointers). */
typedef void (*yac_chunkmix4_fn)(uint32_t *const *out, uint32_t *const *in, uint32_t *const *xor_in);
/* NULL unless a fused implementation is compiled in (AVX2 builds). */
extern yac_chunkmix2_fn const yac_scrypt_chunkmix2;
extern yac_chunkmix4_fn const yac_scrypt_chunkmix4;

/* The unmodified scrypt-jane function (allocates and frees per call). */
int scrypt(const unsigned char *password, size_t password_len, const unsigned char *salt,
           size_t salt_len, unsigned char Nfactor, unsigned char rfactor, unsigned char pfactor,
           unsigned char *out, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif
