# T-10: Fused 2-lane AVX2 ChunkMix

- Depends on: T-09
- Size: M
- Owner: Claude (subagent)
- Started: 2026-10-03
- Finished: 2026-10-04

## Goal
Overlap compute as well as memory. One ChaCha/8 core is a single serial
dependency chain that leaves most execution ports idle, and the T-09 lanes
can't interleave inside the asm `ChunkMix`. A fused core runs two lanes in
one instruction stream: lane A in the low and lane B in the high 128 bits of
each ymm register. `vpshufb`/`vpshufd` work per 128-bit half, so this is close
to a transliteration of `mix_chacha-avx.h:145-246`, with the same register
count and no spills (plan §12).

## Steps
1. Write the 2-lane AVX2 `ChunkMix` in our own source (`src/hash/`), with
   intrinsics or asm. Leave the copied files unchanged.
2. Use it in the T-09 lane loop for lane pairs; L = 2, 4 or 6 then means 1, 2
   or 3 fused pairs. Keep the T-09 single-lane path as the fallback.
3. The binary is built with `-march=native`, so a runtime `cpu_supports`
   check means nothing. Either select at build time, or compile this file
   with `__attribute__((target("avx2")))` and add a check if a portable build
   is wanted.
4. **Tests:**
   - a unit test: fused core versus two calls of the scalar/AVX `ChunkMix` on
     random chunks, bit for bit;
   - all T-09 tests (per-lane known-answer, differential, lane-to-nonce)
     with the fused core.
5. **Benchmark** (plan §12 method): the best T-09 settings versus the same
   lanes with the fused core, inside the memory budget.
6. Reviewer subagent; commit; update README "Performance".

## Acceptance criteria
- [x] The fused core is bit-identical to the existing ChunkMix; all tests pass.
- [x] Enabled by default only if faster than T-09's best by more than the
  run-to-run spread; table in the Log.

## Log
- Code: `src/hash/chacha_avx2x2.c` (intrinsics, compiled with the target's
  flags; empty without `__AVX2__`):
  - `yac_chunkmix2_avx2`: lane A in the low, lane B in the high 128 bits of
    each ymm; every instruction of scrypt-jane's AVX ChunkMix becomes the
    same instruction on ymm (vpshufb/vpshufd per 128-bit half; rot 12/7 by
    shift+or). Same register count, no spills.
  - `yac_chunkmix4_avx2` (extra): two fused pairs interleaved in one loop.
  - Used by T-09's lane loop for lane groups (`yac_mix_step`), fill and
    read pass; `--mix plain|fused2|fused4|auto`. Selection at build time:
    CMake defines `YAC_FUSED_MIX`, and the pointers are set only when the
    compiler targets AVX2 (`-march=native` here). The binary is built for
    the CPU it runs on, so no run-time check (step 3). A non-AVX2 build
    (`-DYAC_NATIVE=OFF`) has no fused mix; `--mix auto` then means plain and
    an explicit `--mix fused*` is refused.
- Tests (native and SSE2 builds pass):
  - `fused_chunkmix_is_bit_identical`: 2000 random chunk sets, with and
    without the xor input, fused2 and fused4 against scrypt-jane's ChunkMix
    called per lane, byte for byte;
  - all T-09 lane tests also run with fused2 and fused4 (prefetch t0 and
    none): per-lane known answers at N-factor 4 for L = 1–8 (odd L mixes
    groups and single lanes), differential random headers with partial
    batches; at N-factor 21: fused2 × 4 lanes and fused4 × 5 lanes against
    the plain path.
  - Integration test `YAC_MINER_ARGS="--lanes 3"` (auto = one fused pair
    plus one plain lane): **PASSED** 2026-10-04 02:08–02:15, 0 verify
    failures.
- Reviewer subagent: reviewed together with T-09 (findings and what was
  applied: T-09 Log). On the fused code: instruction-for-instruction equal
  to the intrinsic `scrypt_ChunkMix_avx`, masks equal
  `ssse3_rotl16/8_32bit`, the 0x93/0x39 order of the second half right,
  interleaving the pairs in fused4 valid. Its finding 1 matters here: with
  lanes = group size the prefetch has no lead time, hence fused2 is also
  measured at 4 lanes per thread. The `auto` default added after the
  review is a small self-reviewed change (`effective_mix()` in main.cpp:
  fused2 only if compiled in and lanes ≥ 2; explicit fused without AVX2 or
  with 1 lane is an error).
- **Benchmark** (same session and method as T-09's table: 180 s, first
  30 s discarded, 3 interleaved repeats, 2026-10-04 00:10–01:52; 60 s
  exploratory runs before it: 7 × 2 fused2 5.95, 8 × 2 fused2 6.32, 4 × 2
  fused2 4.20, 4 × 4 fused4 5.32 vs fused2 5.60, 6 × 4 fused4 6.32 vs
  fused2 6.62, 7 × 3 fused2 6.06; fused4 lost to fused2 in both
  comparisons, so it was dropped from the grid):

| Config | Memory | Runs (H/s) | Median | Min–max | Spread | vs 7 × 1 | vs same lanes plain |
|---|---|---|---|---|---|---|---|
| 7 × 1 (baseline) | 3.5 GiB | 4.152, 4.162, 4.160 | 4.160 | 4.152–4.162 | 0.2 % | – | – |
| 7 × 2 plain (T-09 best at 7 threads) | 7 GiB | 5.127, 5.098, 5.104 | 5.104 | 5.098–5.127 | 0.6 % | +22.7 % | – |
| **7 × 2 fused2** | 7 GiB | 5.975, 5.972, 5.956 | **5.972** | 5.956–5.975 | 0.3 % | **+43.6 %** | **+17.0 %** |
| 7 × 3 fused2 | 10.5 GiB | 6.070, 6.050, 6.064 | 6.064 | 6.050–6.070 | 0.3 % | +45.8 % | +18.2 % (vs 7 × 3 plain 5.131) |
| 4 × 4 fused2 | 8 GiB | 5.602, 5.610, 5.608 | 5.608 | 5.602–5.610 | 0.1 % | +34.8 % | – |
| 6 × 4 fused2 | 12 GiB | 6.598, 6.586, 6.601 | 6.598 | 6.586–6.601 | 0.2 % | +58.6 % | – |

  fused2 beats T-09's best plain setting at equal memory (7 × 2) by 17 %,
  far beyond the spreads (≤ 0.6 %): **enabled by default** (`mix=auto`).
  The CPU clock reads lower with fused2 (busy-CPU mean 1.70 vs 1.89 GHz;
  256-bit work at the platform cap) but the rate is higher.
- **Defaults:** 7 threads (Q5) × 2 lanes × fused2 = 5.97 H/s, 7 GiB.
  Expected time per block 1,048,577 / 5.972 = 175,600 s ≈ **48.8 h**
  (was 70 h). 6 × 4 fused2 (6.60 H/s, 44 h) is faster but needs the whole
  12 GiB budget; left to the owner (README). 7 × 3 adds 1.5 % for 3.5 GiB.
- **Deployed 2026-10-04 02:17:02** (commit c0adde4; `build/` rebuilt, tests
  passed, mainnet miner restarted with the new defaults 7 × 2, mix auto =
  fused2): first stats line 5.83 H/s, then 5.93 H/s, in line with the
  benchmark's 5.97. The owner later switched the live miner to 6 × 4 fused2
  (6.5–6.6 H/s; T-07 Log, 2026-10-04 06:45).
