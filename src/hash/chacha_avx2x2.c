/*
 * yacoin-cpuminer: fused 2-lane ChaCha20/8 ChunkMix with AVX2 (task T-10,
 * plan §12).
 *
 * Two independent lanes in one instruction stream: lane A in the low and
 * lane B in the high 128 bits of each ymm register. Row i of the 4x4 ChaCha
 * state (words 4i..4i+3 of a 64-byte block) is register x_i, exactly as in
 * scrypt-jane's AVX code (third_party/scrypt-jane/code/
 * scrypt-jane-mix_chacha-avx.h, the x86-64 asm and the intrinsic version).
 * vpshufb and vpshufd work on each 128-bit half separately, so every
 * instruction of that code becomes the same instruction on ymm registers, and
 * the result is bit-identical to two calls of scrypt_ChunkMix_avx with r = 1
 * (tests/test_hash.cpp checks it on random chunks).
 *
 * Built only when the compiler targets AVX2 (-march=native on an AVX2 CPU);
 * yac_scrypt.c then points yac_scrypt_chunkmix2 and yac_scrypt_chunkmix4 at
 * the two functions below. The binary is built
 * for the CPU it runs on, so there is no run-time CPU check.
 *
 * Copyright (c) 2026 yacoin-cpuminer contributors. MIT licence (see LICENSE).
 */
#if defined(__AVX2__)

#include <immintrin.h>
#include <stdint.h>

#include "yac_scrypt.h"

static inline __m256i load2(const uint32_t *a, const uint32_t *b)
{
    return _mm256_inserti128_si256(_mm256_castsi128_si256(_mm_load_si128((const __m128i *)a)),
                                   _mm_load_si128((const __m128i *)b), 1);
}

static inline void store2(uint32_t *a, uint32_t *b, __m256i v)
{
    _mm_store_si128((__m128i *)a, _mm256_castsi256_si128(v));
    _mm_store_si128((__m128i *)b, _mm256_extracti128_si256(v, 1));
}

#define ROTL(v, n) _mm256_or_si256(_mm256_slli_epi32(v, n), _mm256_srli_epi32(v, 32 - (n)))

/* out = ChunkMix(in ^ xor) for r = 1 (two 64-byte blocks), lanes A and B.
 * xor_a and xor_b are both NULL or both non-NULL (the lane loop passes the
 * same kind for both). All chunks are 16-byte aligned (tables are 128-byte
 * aligned; X and Y follow them). */
void yac_chunkmix2_avx2(uint32_t *out_a, uint32_t *in_a, uint32_t *xor_a, uint32_t *out_b, uint32_t *in_b,
                        uint32_t *xor_b)
{
    const __m256i r16 = _mm256_setr_epi8(2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13,
                                         2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13);
    const __m256i r8 = _mm256_setr_epi8(3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14,
                                        3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14);
    __m256i x0, x1, x2, x3, t0, t1, t2, t3;
    int i, rounds;

    /* 1: X = B_1 (^ Bxor_1) */
    x0 = load2(in_a + 16, in_b + 16);
    x1 = load2(in_a + 20, in_b + 20);
    x2 = load2(in_a + 24, in_b + 24);
    x3 = load2(in_a + 28, in_b + 28);
    if (xor_a) {
        x0 = _mm256_xor_si256(x0, load2(xor_a + 16, xor_b + 16));
        x1 = _mm256_xor_si256(x1, load2(xor_a + 20, xor_b + 20));
        x2 = _mm256_xor_si256(x2, load2(xor_a + 24, xor_b + 24));
        x3 = _mm256_xor_si256(x3, load2(xor_a + 28, xor_b + 28));
    }

    /* 2: for i = 0 to 1: X = H(X ^ B_i (^ Bxor_i)); Bout_i = X */
    for (i = 0; i < 2; i++) {
        const int o = 16 * i;
        x0 = _mm256_xor_si256(x0, load2(in_a + o + 0, in_b + o + 0));
        x1 = _mm256_xor_si256(x1, load2(in_a + o + 4, in_b + o + 4));
        x2 = _mm256_xor_si256(x2, load2(in_a + o + 8, in_b + o + 8));
        x3 = _mm256_xor_si256(x3, load2(in_a + o + 12, in_b + o + 12));
        if (xor_a) {
            x0 = _mm256_xor_si256(x0, load2(xor_a + o + 0, xor_b + o + 0));
            x1 = _mm256_xor_si256(x1, load2(xor_a + o + 4, xor_b + o + 4));
            x2 = _mm256_xor_si256(x2, load2(xor_a + o + 8, xor_b + o + 8));
            x3 = _mm256_xor_si256(x3, load2(xor_a + o + 12, xor_b + o + 12));
        }
        t0 = x0;
        t1 = x1;
        t2 = x2;
        t3 = x3;
        for (rounds = 8; rounds; rounds -= 2) {
            x0 = _mm256_add_epi32(x0, x1);
            x3 = _mm256_xor_si256(x3, x0);
            x3 = _mm256_shuffle_epi8(x3, r16);
            x2 = _mm256_add_epi32(x2, x3);
            x1 = _mm256_xor_si256(x1, x2);
            x1 = ROTL(x1, 12);
            x0 = _mm256_add_epi32(x0, x1);
            x3 = _mm256_xor_si256(x3, x0);
            x3 = _mm256_shuffle_epi8(x3, r8);
            x0 = _mm256_shuffle_epi32(x0, 0x93);
            x2 = _mm256_add_epi32(x2, x3);
            x3 = _mm256_shuffle_epi32(x3, 0x4e);
            x1 = _mm256_xor_si256(x1, x2);
            x2 = _mm256_shuffle_epi32(x2, 0x39);
            x1 = ROTL(x1, 7);
            x0 = _mm256_add_epi32(x0, x1);
            x3 = _mm256_xor_si256(x3, x0);
            x3 = _mm256_shuffle_epi8(x3, r16);
            x2 = _mm256_add_epi32(x2, x3);
            x1 = _mm256_xor_si256(x1, x2);
            x1 = ROTL(x1, 12);
            x0 = _mm256_add_epi32(x0, x1);
            x3 = _mm256_xor_si256(x3, x0);
            x3 = _mm256_shuffle_epi8(x3, r8);
            x0 = _mm256_shuffle_epi32(x0, 0x39);
            x2 = _mm256_add_epi32(x2, x3);
            x3 = _mm256_shuffle_epi32(x3, 0x4e);
            x1 = _mm256_xor_si256(x1, x2);
            x2 = _mm256_shuffle_epi32(x2, 0x93);
            x1 = ROTL(x1, 7);
        }
        x0 = _mm256_add_epi32(x0, t0);
        x1 = _mm256_add_epi32(x1, t1);
        x2 = _mm256_add_epi32(x2, t2);
        x3 = _mm256_add_epi32(x3, t3);
        /* r = 1: Y_0 goes to block 0 and Y_1 to block 1 of the output. */
        store2(out_a + o + 0, out_b + o + 0, x0);
        store2(out_a + o + 4, out_b + o + 4, x1);
        store2(out_a + o + 8, out_b + o + 8, x2);
        store2(out_a + o + 12, out_b + o + 12, x3);
    }
}


/* Two fused pairs (lanes A, B in p and C, D in q) in one loop: twice the
 * independent work per instruction window, for a thread whose core has no
 * SMT sibling busy. Same result as two yac_chunkmix2_avx2 calls. out/in/xor
 * hold the four lanes' chunk pointers; xor is NULL or holds four pointers. */
void yac_chunkmix4_avx2(uint32_t *const *out, uint32_t *const *in, uint32_t *const *xor_in)
{
    const __m256i r16 = _mm256_setr_epi8(2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13,
                                         2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13);
    const __m256i r8 = _mm256_setr_epi8(3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14,
                                        3, 0, 1, 2, 7, 4, 5, 6, 11, 8, 9, 10, 15, 12, 13, 14);
    __m256i p0, p1, p2, p3, q0, q1, q2, q3, tp0, tp1, tp2, tp3, tq0, tq1, tq2, tq3;
    int i, rounds;

    p0 = load2(in[0] + 16, in[1] + 16);
    p1 = load2(in[0] + 20, in[1] + 20);
    p2 = load2(in[0] + 24, in[1] + 24);
    p3 = load2(in[0] + 28, in[1] + 28);
    q0 = load2(in[2] + 16, in[3] + 16);
    q1 = load2(in[2] + 20, in[3] + 20);
    q2 = load2(in[2] + 24, in[3] + 24);
    q3 = load2(in[2] + 28, in[3] + 28);
    if (xor_in) {
        p0 = _mm256_xor_si256(p0, load2(xor_in[0] + 16, xor_in[1] + 16));
        p1 = _mm256_xor_si256(p1, load2(xor_in[0] + 20, xor_in[1] + 20));
        p2 = _mm256_xor_si256(p2, load2(xor_in[0] + 24, xor_in[1] + 24));
        p3 = _mm256_xor_si256(p3, load2(xor_in[0] + 28, xor_in[1] + 28));
        q0 = _mm256_xor_si256(q0, load2(xor_in[2] + 16, xor_in[3] + 16));
        q1 = _mm256_xor_si256(q1, load2(xor_in[2] + 20, xor_in[3] + 20));
        q2 = _mm256_xor_si256(q2, load2(xor_in[2] + 24, xor_in[3] + 24));
        q3 = _mm256_xor_si256(q3, load2(xor_in[2] + 28, xor_in[3] + 28));
    }

    for (i = 0; i < 2; i++) {
        const int o = 16 * i;
        p0 = _mm256_xor_si256(p0, load2(in[0] + o + 0, in[1] + o + 0));
        p1 = _mm256_xor_si256(p1, load2(in[0] + o + 4, in[1] + o + 4));
        p2 = _mm256_xor_si256(p2, load2(in[0] + o + 8, in[1] + o + 8));
        p3 = _mm256_xor_si256(p3, load2(in[0] + o + 12, in[1] + o + 12));
        q0 = _mm256_xor_si256(q0, load2(in[2] + o + 0, in[3] + o + 0));
        q1 = _mm256_xor_si256(q1, load2(in[2] + o + 4, in[3] + o + 4));
        q2 = _mm256_xor_si256(q2, load2(in[2] + o + 8, in[3] + o + 8));
        q3 = _mm256_xor_si256(q3, load2(in[2] + o + 12, in[3] + o + 12));
        if (xor_in) {
            p0 = _mm256_xor_si256(p0, load2(xor_in[0] + o + 0, xor_in[1] + o + 0));
            p1 = _mm256_xor_si256(p1, load2(xor_in[0] + o + 4, xor_in[1] + o + 4));
            p2 = _mm256_xor_si256(p2, load2(xor_in[0] + o + 8, xor_in[1] + o + 8));
            p3 = _mm256_xor_si256(p3, load2(xor_in[0] + o + 12, xor_in[1] + o + 12));
            q0 = _mm256_xor_si256(q0, load2(xor_in[2] + o + 0, xor_in[3] + o + 0));
            q1 = _mm256_xor_si256(q1, load2(xor_in[2] + o + 4, xor_in[3] + o + 4));
            q2 = _mm256_xor_si256(q2, load2(xor_in[2] + o + 8, xor_in[3] + o + 8));
            q3 = _mm256_xor_si256(q3, load2(xor_in[2] + o + 12, xor_in[3] + o + 12));
        }
        tp0 = p0; tp1 = p1; tp2 = p2; tp3 = p3;
        tq0 = q0; tq1 = q1; tq2 = q2; tq3 = q3;
        for (rounds = 8; rounds; rounds -= 2) {
#define HALF(a0, a1, a2, a3, S0, S2)              \
            a0 = _mm256_add_epi32(a0, a1);        \
            a3 = _mm256_xor_si256(a3, a0);        \
            a3 = _mm256_shuffle_epi8(a3, r16);    \
            a2 = _mm256_add_epi32(a2, a3);        \
            a1 = _mm256_xor_si256(a1, a2);        \
            a1 = ROTL(a1, 12);                    \
            a0 = _mm256_add_epi32(a0, a1);        \
            a3 = _mm256_xor_si256(a3, a0);        \
            a3 = _mm256_shuffle_epi8(a3, r8);     \
            a0 = _mm256_shuffle_epi32(a0, S0);    \
            a2 = _mm256_add_epi32(a2, a3);        \
            a3 = _mm256_shuffle_epi32(a3, 0x4e);  \
            a1 = _mm256_xor_si256(a1, a2);        \
            a2 = _mm256_shuffle_epi32(a2, S2);    \
            a1 = ROTL(a1, 7);
            HALF(p0, p1, p2, p3, 0x93, 0x39)
            HALF(q0, q1, q2, q3, 0x93, 0x39)
            HALF(p0, p1, p2, p3, 0x39, 0x93)
            HALF(q0, q1, q2, q3, 0x39, 0x93)
#undef HALF
        }
        p0 = _mm256_add_epi32(p0, tp0);
        p1 = _mm256_add_epi32(p1, tp1);
        p2 = _mm256_add_epi32(p2, tp2);
        p3 = _mm256_add_epi32(p3, tp3);
        q0 = _mm256_add_epi32(q0, tq0);
        q1 = _mm256_add_epi32(q1, tq1);
        q2 = _mm256_add_epi32(q2, tq2);
        q3 = _mm256_add_epi32(q3, tq3);
        store2(out[0] + o + 0, out[1] + o + 0, p0);
        store2(out[0] + o + 4, out[1] + o + 4, p1);
        store2(out[0] + o + 8, out[1] + o + 8, p2);
        store2(out[0] + o + 12, out[1] + o + 12, p3);
        store2(out[2] + o + 0, out[3] + o + 0, q0);
        store2(out[2] + o + 4, out[3] + o + 4, q1);
        store2(out[2] + o + 8, out[3] + o + 8, q2);
        store2(out[2] + o + 12, out[3] + o + 12, q3);
    }
}

#endif /* __AVX2__ */
