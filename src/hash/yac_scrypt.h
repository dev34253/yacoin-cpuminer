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

/* The unmodified scrypt-jane function (allocates and frees per call). */
int scrypt(const unsigned char *password, size_t password_len, const unsigned char *salt,
           size_t salt_len, unsigned char Nfactor, unsigned char rfactor, unsigned char pfactor,
           unsigned char *out, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif
