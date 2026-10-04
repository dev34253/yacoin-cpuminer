# T-10: Fused 2-lane AVX2 ChunkMix

- Depends on: T-09
- Size: M
- Owner:
- Started:
- Finished:

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
- [ ] The fused core is bit-identical to the existing ChunkMix; all tests pass.
- [ ] Enabled by default only if faster than T-09's best by more than the
  run-to-run spread; table in the Log.

## Log
-
