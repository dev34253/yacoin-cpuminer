/* yacoin-cpuminer: C entry points around the copied scrypt-jane. MIT licence. */
#ifndef YAC_SCRYPT_H
#define YAC_SCRYPT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runs scrypt-jane's power-on self-test (mix, hash, scrypt vectors).
 * Returns 1 on success. Not thread-safe: call once from main before workers
 * start. With the fatal-error handler left at its default, a failure inside
 * scrypt-jane calls exit(21); install a handler with scrypt_set_fatal_error. */
int yac_scrypt_self_test(void);

/* Name of the ChaCha mix variant compiled in, e.g. "ChaCha/8-AVX". */
const char *yac_scrypt_mix_name(void);

#ifdef __cplusplus
}
#endif

#endif
