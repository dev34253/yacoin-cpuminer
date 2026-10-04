# T-09: Several hashes per thread (lanes) with prefetch

- Depends on: T-08
- Size: M
- Owner:
- Started:
- Finished:

## Goal
Overlap DRAM reads by computing L independent hashes (each with its own
512 MiB table) per thread in the miner's own ROMix loop, with explicit
prefetch (plan §12). This step overlaps memory only; compute overlap is T-10.

## Steps
1. **Design** (write it into this file first):
   - Our own ROMix copy in `src/hash/`. It calls scrypt-jane's static
     `scrypt_ChunkMix_avx`; `yac_scrypt.c` includes `scrypt-jane.c`, so the
     copied files stay unchanged.
   - Lanes alternate chunk by chunk in the read pass. The fill pass is
     compute-bound, so lanes there neither help nor hurt; keep it simple.
   - **Prefetch:** as soon as lane k's next index `j` is known (after the
     second ChaCha core of its chunk, `romix-template.h:96,102`), prefetch
     **both** 64-byte lines of chunk `j`, then mix the other lanes. The
     prefetch distance is about (L−1) × 65 ns, so expect L ≥ 3 to be needed.
     Try T0 versus NTA hints.
   - **Alignment:** 128-byte alignment of every table (today the check is 64,
     `yac_scrypt.c:46`).
2. **Miner changes:**
   - `--lanes N` / `lanes=`; each worker takes L nonces per batch, including
     a partial last batch when the nonce slice isn't a multiple of L
     (`miner.cpp:201`).
   - Stats add L hashes per batch.
   - The memory check counts threads × lanes × scratch.
   - **Before every submit, recompute the reference hash of the exact header
     being submitted** and drop the solution, with an error, if it doesn't
     meet the target.
3. **Tests:**
   - known-answer tests for each lane position at L = 1–4;
   - a differential test with a **different header per lane** and random
     nonces, at N-factor 4 (many) and N-factor 21 (a few, including L = 4, to
     catch offset overflow if lanes share one allocation);
   - unit tests for the lane-to-nonce mapping and the partial last batch;
   - the integration test on the test chain with `--lanes 3`.
4. **Benchmark** (plan §12 method, mainnet miner stopped):
   - grid inside lanes × threads × 0.5 GiB ≤ 12 GiB, for example L=1×{4,7,8},
     L=2×{4,6,7,8}, L=3×{4,6,7};
   - include equal-memory comparisons (4×2 versus 8×1);
   - 1-lane baseline re-measured in the same session.
5. Choose default lanes and threads; update README "Performance" and
   `contrib/miner.conf.example`. Reviewer subagent on the diff; commit.

## Acceptance criteria
- [ ] All tests pass, including the per-lane known-answer, differential,
  lane-to-nonce and partial-batch tests.
- [ ] Benchmark table (median and min–max of 3+ interleaved runs) in the Log.
  The default changes only if it beats the same-session baseline by more than
  the run-to-run spread.
- [ ] Mainnet miner restarted with the chosen settings; first stats line in
  the Log.

## Log
-
