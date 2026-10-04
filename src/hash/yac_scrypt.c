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

/* ------------------------------------------------------------------------
 * Several hashes per call ("lanes", task T-09, plan §12).
 *
 * Same algorithm as scrypt_ROMix in scrypt-jane-romix-template.h with r = 1,
 * but for `lanes` independent hashes, each with its own table V. The lanes
 * take turns chunk by chunk, and as soon as a lane's next random index j is
 * known (it is word 16 of the chunk just mixed), both 64-byte lines of
 * V_j are prefetched. While the other lanes mix, that DRAM read is in flight.
 * The mix function is the one scrypt-jane chose at compile time
 * (YAC_CHUNKMIX below); the copied files are unchanged.
 * ------------------------------------------------------------------------ */

#if defined(SCRYPT_CHACHA_XOP)
#define YAC_CHUNKMIX scrypt_ChunkMix_xop
#elif defined(SCRYPT_CHACHA_AVX)
#define YAC_CHUNKMIX scrypt_ChunkMix_avx
#elif defined(SCRYPT_CHACHA_SSSE3)
#define YAC_CHUNKMIX scrypt_ChunkMix_ssse3
#elif defined(SCRYPT_CHACHA_SSE2)
#define YAC_CHUNKMIX scrypt_ChunkMix_sse2
#else
#error "lanes need one of scrypt-jane's x86 ChaCha mix variants (SSE2 or better)"
#endif

#include <xmmintrin.h>

#define YAC_CHUNK_WORDS (YAC_CHUNK_BYTES / 4) /* 32 */

void yac_scrypt_chunkmix(uint32_t *out, uint32_t *in, uint32_t *xor_in)
{
    YAC_CHUNKMIX(out, in, xor_in, 1);
}

static inline void yac_prefetch_chunk(const uint32_t *p, unsigned hint)
{
    /* Both 64-byte lines of the 128-byte chunk (tables are 128-byte aligned). */
    if (hint == YAC_PREFETCH_T0) {
        _mm_prefetch((const char *)p, _MM_HINT_T0);
        _mm_prefetch((const char *)p + 64, _MM_HINT_T0);
    } else if (hint == YAC_PREFETCH_NTA) {
        _mm_prefetch((const char *)p, _MM_HINT_NTA);
        _mm_prefetch((const char *)p + 64, _MM_HINT_NTA);
    }
}

/* Fused multi-lane ChunkMix (T-10, src/hash/chacha_avx2x2.c). That file
 * compiles to nothing without AVX2; CMake defines YAC_FUSED_MIX for the whole
 * yac_scrypt target, which builds both files with the same flags. */
#if defined(__AVX2__) && defined(YAC_FUSED_MIX)
void yac_chunkmix2_avx2(uint32_t *out_a, uint32_t *in_a, uint32_t *xor_a, uint32_t *out_b, uint32_t *in_b,
                        uint32_t *xor_b);
void yac_chunkmix4_avx2(uint32_t *const *out, uint32_t *const *in, uint32_t *const *xor_in);
yac_chunkmix2_fn const yac_scrypt_chunkmix2 = yac_chunkmix2_avx2;
yac_chunkmix4_fn const yac_scrypt_chunkmix4 = yac_chunkmix4_avx2;
#else
yac_chunkmix2_fn const yac_scrypt_chunkmix2 = 0;
yac_chunkmix4_fn const yac_scrypt_chunkmix4 = 0;
#endif

/* One mix step for all lanes: out[k] = ChunkMix(in[k] ^ xr[k]) (xr NULL in
 * the fill pass). Lanes go in groups of 4 or 2 when a fused mix is chosen,
 * the rest one by one. After each group, every lane of it prefetches its next
 * random chunk V_j, j = out[k][16] mod N (read pass only); the lead time is
 * the time the following groups take to mix (none if there is one group). */
static inline void yac_mix_step(uint32_t *const *out, uint32_t *const *in, uint32_t *const *xr, unsigned lanes,
                                unsigned group, uint32_t *const *V, uint32_t mask, unsigned pf)
{
    unsigned k = 0, q;
    while (k < lanes) {
        unsigned n;
        if (group >= 4 && k + 4 <= lanes) {
            yac_scrypt_chunkmix4(out + k, in + k, xr ? xr + k : NULL);
            n = 4;
        } else if (group >= 2 && k + 2 <= lanes) {
            yac_scrypt_chunkmix2(out[k], in[k], xr ? xr[k] : NULL, out[k + 1], in[k + 1], xr ? xr[k + 1] : NULL);
            n = 2;
        } else {
            YAC_CHUNKMIX(out[k], in[k], xr ? xr[k] : NULL, 1);
            n = 1;
        }
        if (V)
            for (q = k; q < k + n; q++)
                yac_prefetch_chunk(V[q] + (size_t)(out[q][YAC_CHUNK_WORDS - 16] & mask) * YAC_CHUNK_WORDS, pf);
        k += n;
    }
}

int yac_scrypt_hash_lanes(const uint8_t *const *inputs, size_t input_len, unsigned nfactor,
                          uint8_t *const *scratch, uint8_t (*out)[32], unsigned lanes, unsigned flags)
{
    uint32_t N, mask, i;
    unsigned k, group = 1;
    const unsigned pf = flags & YAC_PREFETCH_MASK;
    uint32_t *V[YAC_SCRYPT_MAX_LANES], *X[YAC_SCRYPT_MAX_LANES], *Y[YAC_SCRYPT_MAX_LANES];
    uint32_t *a[YAC_SCRYPT_MAX_LANES], *b[YAC_SCRYPT_MAX_LANES], *vj[YAC_SCRYPT_MAX_LANES];

    if (nfactor > YAC_SCRYPT_MAX_NFACTOR || lanes < 1 || lanes > YAC_SCRYPT_MAX_LANES)
        return 0;
    for (k = 0; k < lanes; k++)
        if (((uintptr_t)scratch[k] & (YAC_CHUNK_BYTES - 1)) != 0)
            return 0;
    if ((flags & YAC_MIX_FUSED4) && yac_scrypt_chunkmix4 && yac_scrypt_chunkmix2)
        group = 4;
    else if ((flags & (YAC_MIX_FUSED2 | YAC_MIX_FUSED4)) && yac_scrypt_chunkmix2)
        group = 2;

    N = (uint32_t)1 << (nfactor + 1);
    mask = N - 1;
    for (k = 0; k < lanes; k++) {
        V[k] = (uint32_t *)scratch[k];
        Y[k] = V[k] + (size_t)N * YAC_CHUNK_WORDS;
        X[k] = Y[k] + YAC_CHUNK_WORDS;
        /* 1: X = PBKDF2(password, salt) */
        scrypt_pbkdf2(inputs[k], input_len, inputs[k], input_len, 1, (uint8_t *)X[k], YAC_CHUNK_BYTES);
        /* ROMix tangle is a no-op for the x86 ChaCha variants. */
        memcpy(V[k], X[k], YAC_CHUNK_BYTES);
    }

    /* 2-4: V_i = X; X = H(X), lanes interleaved chunk by chunk. */
    for (i = 0; i < N - 1; i++) {
        const size_t o = (size_t)i * YAC_CHUNK_WORDS;
        for (k = 0; k < lanes; k++) {
            a[k] = V[k] + o + YAC_CHUNK_WORDS;
            b[k] = V[k] + o;
        }
        yac_mix_step(a, b, NULL, lanes, group, NULL, mask, pf);
    }
    for (k = 0; k < lanes; k++)
        b[k] = V[k] + (size_t)(N - 1) * YAC_CHUNK_WORDS;
    yac_mix_step(X, b, NULL, lanes, group, V, mask, pf); /* also prefetches the first V_j */

    /* 6-8: N random reads; j = Integerify(X) mod N; X = H(X ^ V_j). As in
     * scrypt-jane, X and Y take turns as input and output. */
    for (i = 0; i < N; i += 2) {
        for (k = 0; k < lanes; k++)
            vj[k] = V[k] + (size_t)(X[k][YAC_CHUNK_WORDS - 16] & mask) * YAC_CHUNK_WORDS;
        yac_mix_step(Y, X, vj, lanes, group, V, mask, pf);
        for (k = 0; k < lanes; k++)
            vj[k] = V[k] + (size_t)(Y[k][YAC_CHUNK_WORDS - 16] & mask) * YAC_CHUNK_WORDS;
        yac_mix_step(X, Y, vj, lanes, group, V, mask, pf);
    }

    /* 3: Out = PBKDF2(password, X) */
    for (k = 0; k < lanes; k++)
        scrypt_pbkdf2(inputs[k], input_len, (const uint8_t *)X[k], YAC_CHUNK_BYTES, 1, out[k], 32);
    return 1;
}
