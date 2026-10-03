/*
 * yacoin-cpuminer: scrypt-jane build unit.
 *
 * This file #includes the unmodified copy of yacoin's scrypt-jane.c (see
 * third_party/scrypt-jane/SOURCE.md), so the mix code is compiled exactly once,
 * with the node's defines (SCRYPT_KECCAK512, SCRYPT_CHACHA,
 * SCRYPT_CHOOSE_COMPILETIME, set by CMake), and adds a few entry points.
 *
 * Copyright (c) 2026 yacoin-cpuminer contributors. MIT licence (see LICENSE).
 */
#include "scrypt-jane.c"

#include "yac_scrypt.h"

/* Yacoin uses r = 2^0 = 1 and p = 2^0 = 1 (src/scrypt.cpp scrypt_hash). */
#define YAC_CHUNK_BYTES (SCRYPT_BLOCK_BYTES * 1 * 2) /* 128 */

int yac_scrypt_self_test(void)
{
    /* The first scrypt() call runs scrypt_power_on_self_test() and sets the
     * static flag inside scrypt(), so later calls (from any thread) never run
     * it again. On failure scrypt-jane prints the reason and calls exit(21). */
    uint8_t out[32];
    return scrypt((const unsigned char *)"yac", 3, (const unsigned char *)"yac", 3, 0, 0, 0, out, sizeof out) == 1;
}

const char *yac_scrypt_mix_name(void)
{
    return SCRYPT_MIX;
}

size_t yac_scrypt_scratch_bytes(unsigned nfactor)
{
    if (nfactor > YAC_SCRYPT_MAX_NFACTOR)
        return 0;
    /* V: N chunks, then Y and X (one chunk each, p = 1). */
    return ((size_t)1 << (nfactor + 1)) * YAC_CHUNK_BYTES + 2 * YAC_CHUNK_BYTES;
}

int yac_scrypt_hash_scratch(const uint8_t *input, size_t input_len, unsigned nfactor,
                            uint8_t *scratch, uint8_t out[32])
{
    uint32_t N;
    uint8_t *V, *Y, *X;

    if (nfactor > YAC_SCRYPT_MAX_NFACTOR || ((uintptr_t)scratch & (SCRYPT_BLOCK_BYTES - 1)) != 0)
        return 0;

    /* Same steps as scrypt() in scrypt-jane.c with salt = input, rfactor = 0,
     * pfactor = 0 and a 32-byte output, but with caller-owned memory. */
    N = (uint32_t)1 << (nfactor + 1);
    V = scratch;
    Y = scratch + (size_t)N * YAC_CHUNK_BYTES;
    X = Y + YAC_CHUNK_BYTES;

    /* 1: X = PBKDF2(password, salt) */
    scrypt_pbkdf2(input, input_len, input, input_len, 1, X, YAC_CHUNK_BYTES);
    /* 2: X = ROMix(X) */
    scrypt_ROMix((scrypt_mix_word_t *)X, (scrypt_mix_word_t *)Y, (scrypt_mix_word_t *)V, N, 1);
    /* 3: Out = PBKDF2(password, X) */
    scrypt_pbkdf2(input, input_len, X, YAC_CHUNK_BYTES, 1, out, 32);
    return 1;
}
