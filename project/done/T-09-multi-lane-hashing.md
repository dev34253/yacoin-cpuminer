# T-09: Several hashes per thread (lanes) with prefetch

- Depends on: T-08
- Size: M
- Owner: Claude (subagent)
- Started: 2026-10-03
- Finished: 2026-10-04

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

## Design (as built)
- `yac_scrypt_hash_lanes()` in `src/hash/yac_scrypt.c` (which `#include`s the
  unchanged `scrypt-jane.c`): PBKDF2 per lane, then scrypt-jane's ROMix with
  r = 1 for L lanes. The mix is the ChunkMix scrypt-jane picked at compile
  time (`scrypt_ChunkMix_avx` with `-march=native`, `_sse2` with
  `YAC_NATIVE=OFF`), called through the `YAC_CHUNKMIX` macro.
- **Fill pass:** lanes take turns chunk by chunk (as simple as one lane after
  the other, and lets the out-of-order core overlap the tail of one ChunkMix
  with the next lane's).
- **Read pass:** lanes take turns chunk by chunk. Right after lane k's
  ChunkMix, its next index `j = out[16] & (N-1)` is known and both 64-byte
  lines of `V_j` are prefetched (`--prefetch t0|nta|none`, default t0); the
  other L−1 lanes mix before lane k reads it.
- **Memory:** one mmap per thread with L tables; each table starts on a
  2 MiB boundary (huge pages) or 4 KiB, so 128-byte alignment holds (checked
  in `yac_scrypt_hash_lanes`). Offsets are `size_t`.
- **`--lanes 1`** keeps the plain scrypt-jane path (`hash_lanes` with one
  input calls `yac_scrypt_hash_scratch`).
- **Miner:** each worker hashes `next_batch()` batches: L consecutive nonces
  `first + k` for lane k, fewer for the last batch of its slice
  (`src/miner/nonce.h`). Stats add the batch size. The memory check counts
  threads × lanes tables plus one for the submit check.
- **Reference re-hash before every submit:** `verify_solution()` rebuilds the
  header from the exact `getwork` submit data, hashes it with the plain
  allocating scrypt-jane call (`pow_hash_reference`) and drops the solution
  with an error (counted as rejected) unless the hash equals the worker's and
  meets the target. `MinerConfig::verify_before_submit` (default on).

## Acceptance criteria
- [x] All tests pass, including the per-lane known-answer, differential,
  lane-to-nonce and partial-batch tests.
- [x] Benchmark table (median and min–max of 3+ interleaved runs) in the Log.
  The default changes only if it beats the same-session baseline by more than
  the run-to-run spread.
- [x] Mainnet miner restarted with the chosen settings; first stats line in
  the Log (deployed together with T-10, see T-10's Log).

## Log
- Design above; code in `src/hash/yac_scrypt.c` (`yac_scrypt_hash_lanes`),
  `src/hash/pow_hash.*` (`ScryptHasher` with L tables, `hash_lanes`),
  `src/miner/nonce.h` (`next_batch`), `src/miner/miner.cpp` (worker batches,
  `verify_solution`, lanes in `run_benchmark`), `src/main.cpp` / options
  (`--lanes`, `--prefetch`, `--mix`; memory check threads × lanes + 1).
  The lane loop already has the hooks for T-10's fused mixes (`--mix`); in
  this commit no fused mix is compiled in, so `--mix fused*` is refused.
- **Tests** (all pass, native AVX build and `-DYAC_NATIVE=OFF` SSE2 build):
  - per-lane known-answer tests: every lane position of L = 1–8 against the
    three test-chain blocks at N-factor 4 (prefetch t0, nta, none), and
    L = 2, 3, 4 against real mainnet block hashes at N-factor 21;
  - differential: random header and nonce per lane, random partial batch
    sizes, L = 1–8, 25 calls each, N-factor 4, against
    `pow_hash_reference`; at N-factor 21 one 5-lane call (last table 2 GiB +
    8 MiB into one allocation, past 2^31);
  - `next_batch`: batches cover a slice exactly, last batch partial, lane k
    = nonce first + k, no wrap at 2^32; bad lane counts throw;
  - `verify_solution`: accepts a real solution; rejects a wrong nonce
    (lane-to-nonce mix-up), a wrong hash, and a hash above the target;
  - in-process miner with 2 threads × 3 lanes finds and submits 5 blocks,
    0 verify failures;
  - benchmark with lanes and warm-up.
- **Reviewer subagent** (on the T-09 + T-10 diff, read-only while benchmarks
  ran): no correctness bug found in the hash path (ROMix equivalence, index
  word, X/Y alternation, alignment, offsets, nonce mapping all checked).
  Applied:
  1. docs: with a fused mix and lanes = group size the prefetch gets no lead
     time (comments, `--mix` help);
  2. `--mix fused*` without an AVX2 build or with `--lanes 1` is now an
     error instead of a silent fallback; `BENCH` line prints mix and
     prefetch;
  4. **the re-hash before submit no longer allocates**: scrypt-jane's
     allocating `scrypt()` calls `exit(21)` on malloc failure, which the
     try/catch could not stop. The submitter now owns a one-lane
     `ScryptHasher` allocated at start (counted by the memory check) and
     re-hashes on its plain scrypt-jane path;
  5. `verify_solution` comment no longer claims it checks the getwork word
     order (test_getwork does);
  6. verify failures get their own counter (`VERIFY FAILED n` in the stats
     line) instead of `rejected`;
  8. `--lanes` help mentions the extra 512 MiB for the submit check;
  9. fused function pointers are `const`; 10. comment fixes;
  11. benchmark: a thread that throws after its warm-up no longer counts
     `ready` twice;
  12. N-factor 21 differential test now covers the fused mixes and L = 5
     (past 2^31); the wrong "L = 4 catches offset overflow" comment fixed.
  Not applied: 3 (new work is noticed only between batches: with 2 lanes a
  batch is ~2.4 s, so a tip change wastes at most that per thread; blocks
  are hours apart; documented here); 7 (with huge pages each table's extra
  256 B for X/Y costs one more 2 MiB page per lane, 14 MiB at 7 × 2:
  negligible, not worth a separate buffer).
- Mainnet miner stopped 2026-10-03 23:07:29 for T-08 and this task's
  benchmarks; restarted with the old binary 2026-10-04 01:53:12 ("new work
  1" logged).
- **Exploratory sweep** (60 s, first 20 s discarded, 1 run; for choosing the
  grid): 7 × 1 4.12, 7 × 2 5.04, 7 × 3 5.09, 8 × 2 5.55, 4 × 2 3.54,
  4 × 3 3.67, **7 × 2 without prefetch 4.06**. So the lane gain is the
  prefetch; this contradicts T-08's "compute-bound at 7 threads" (corrected
  in plan §12: the single-lane limit is the latency of one serial chain per
  hardware thread, which the core can overlap across lanes once the DRAM
  miss is prefetched).
- **Benchmark** (plan §12 method: `scripts/bench.sh`, 180 s, first 30 s
  discarded, 3 interleaved repeats over all configurations of T-09 and T-10
  together, 2026-10-04 00:10–01:52; AC, governor `powersave`, EPP
  `balance_performance`, THP `madvise`, node idle 0 %, max package 66 °C;
  mean busy-CPU clock 1.77–1.89 GHz by `scaling_cur_freq`):

| Config | Memory | Runs (H/s) | Median | Min–max | Spread | vs 7 × 1 |
|---|---|---|---|---|---|---|
| 7 × 1 (baseline) | 3.5 GiB | 4.152, 4.162, 4.160 | 4.160 | 4.152–4.162 | 0.2 % | – |
| 8 × 1 | 4 GiB | 4.595, 4.583, 4.582 | 4.583 | 4.582–4.595 | 0.3 % | +10.2 % |
| 4 × 2 | 4 GiB | 3.614, 3.591, 3.625 | 3.614 | 3.591–3.625 | 0.9 % | −13.1 % |
| 7 × 2 | 7 GiB | 5.127, 5.098, 5.104 | 5.104 | 5.098–5.127 | 0.6 % | **+22.7 %** |
| 7 × 2 nta | 7 GiB | 5.004, 4.991, 4.959 | 4.991 | 4.959–5.004 | 0.9 % | +20.0 % |
| 7 × 3 | 10.5 GiB | 5.217, 5.131, 5.095 | 5.131 | 5.096–5.217 | 2.4 % | +23.3 % |
| 8 × 2 | 8 GiB | 5.455, 5.410, 5.452 | 5.452 | 5.410–5.455 | 0.8 % | +31.1 % |

  Equal memory: 4 × 2 (3.61) loses to 8 × 1 (4.58): at equal memory, more
  threads beat more lanes; lanes pay only on top of the threads.
- **Defaults:** `lanes` 2 (7 × 2 beats the same-session baseline by 22.7 %,
  far more than the 0.2–0.6 % spreads; a third lane is within the spread),
  prefetch t0 (beats nta by 2.3 %, more than the spread). Threads stay 7
  (Q5). README "Performance" and `contrib/miner.conf.example` updated.
- Integration test (`tests/integration.sh` with
  `YAC_MINER=build-dev/yacoin-cpuminer YAC_MINER_ARGS="--lanes 3"`, new env
  hooks in the script): **PASSED** 2026-10-04 01:56–02:02 (2 threads × 3
  lanes; accept, ramp, stale, retry phases; 0 verify failures).
- Deployment: combined with T-10 (one stop/rebuild/restart instead of two),
  see T-10's log.
