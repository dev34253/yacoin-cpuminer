# T-06: Performance measurements and defaults

- Depends on: T-04
- Size: S
- Owner: Claude (subagent)
- Started: 2026-10-03
- Finished: 2026-10-03

## Goal
Replace the plan's guesses (plan §4, §5) with numbers and pick defaults.

## Steps
1. H/s at N-factor 21 for 1–8 threads, with and without `-march=native`, with and without huge pages; compare with yacoind's built-in miner (`setgenerate`, measured briefly on the lowdiff test chain at N-factor 21 or by `hashespersec`).
2. Effect on the running mainnet node (CPU, memory) and laptop temperature.
3. Pick the default threads and nice level (Q5) and document the numbers.
4. Expected time per block from the measured rate and the network estimate.

## Acceptance criteria
- [x] Table of results in the task log; defaults set in the code and README.

## Log
- Method: `scripts/bench.sh` runs `yacoin-cpuminer --benchmark` (no node) for
  90 s per thread count under `nice -n 10`, after one warm-up hash per
  thread, and samples the CPU package temperature (`x86_pkg_temp`), the
  miner's `AnonHugePages` and the mainnet yacoind's CPU time. Never more
  than 8 threads. Laptop: i5-8300H (4 cores / 8 threads), 30 GiB, THP
  `madvise`; mainnet node running (idle, at height 1,964,617).
- Built-in miner (step 1): `scripts/builtin-bench.sh` starts a third,
  fully isolated low-difficulty node (no P2P: `-listen=0 -connect=0`; RPC
  127.0.0.1:27692; datadir `testchain/data/node3`, wiped first) with
  `-nFactorAtHardfork=21 -testnetNewLogicBlockNumber=0`, runs `setgenerate
  true N` for 150 s and reads the node's 60 s `hash count` lines: 1 thread
  38–39 hashes/60 s, 4 threads 93–97 hashes/60 s. Never on mainnet. (Its
  `hashmeter` line prints 0/1 because it formats an integer with `%.1f`.)

| Build | Threads | Huge pages | Total H/s | Per thread H/s | Max pkg temp | Expected time per block* |
|---|---|---|---|---|---|---|
| AVX (`-march=native`) | 1 | on | 0.945 | 0.945 | 49 °C | 308 h |
| AVX | 2 | on | 1.672 | 0.84 | 49 °C | 174 h |
| AVX | 3 | on | 2.224 | 0.74 | 52 °C | 131 h |
| AVX | 4 | on | 2.788 | 0.70 | 52 °C | 104 h |
| AVX | 5 | on | 3.237 | 0.58–0.69 | 54 °C | 90 h |
| AVX | 6 | on | 3.697 | 0.58–0.69 | 55 °C | 79 h |
| **AVX** | **7** | **on** | **4.152** | 0.58–0.67 | 56 °C | **70 h (2.9 days)** |
| AVX | 8 | on | 4.584 | 0.57 | 57 °C | 64 h |
| AVX | 1 | off | 0.863 | 0.863 | 56 °C | 338 h |
| AVX | 4 | off | 2.372 | 0.59 | 54 °C | 123 h |
| AVX | 7 | off | 3.487 | 0.49–0.52 | 56 °C | 84 h |
| SSE2 (`-msse2`, node baseline) | 1 | on | 0.859 | 0.859 | 55 °C | 339 h |
| SSE2 | 4 | on | 2.535 | 0.63 | 54 °C | 115 h |
| SSE2 | 7 | on | 3.795 | 0.53 | 56 °C | 77 h |
| yacoind built-in miner (`setgenerate`, test node at N-factor 21) | 1 | – | 0.64 | 0.64 | – | 455 h |
| yacoind built-in miner | 4 | – | 1.58 | 0.40 | – | 184 h |

\* 1,048,577 hashes per block at mainnet's fixed minimum difficulty `1e0fffff` (plan §4) ÷ rate.

- Huge pages: `AnonHugePages` = 512 MiB per thread with `--hugepages`, 0
  without, so THP is really used.
- Step 2, effect on the mainnet node: the node stayed idle (0–4 % CPU over
  each run; RSS 1.4 GiB unchanged); `MemAvailable` ≈ 19–20 GiB before the
  runs, 8 threads use 4 GiB. Max package temperature 57 °C at 8 threads.
  Not measured: the node under load (it was not syncing).
- Step 3, defaults: **threads 7 confirmed** (Q5, one hardware thread free; 8
  would give only +10 %), **nice 10** kept, **huge pages on**,
  **`-march=native` on** for local builds. Set in `src/app/options.h`,
  `src/app/options.cpp` and the README.
- Step 4: at 7 threads, 1,048,577 / 4.152 H/s ≈ 252,500 s ≈ **70 h (2.9
  days) per block on average**, ≈ 0.34 blocks/day ≈ 1.8 YAC/day at 5.17
  YAC per block. Network estimate (`netmhashps` 5.4e-05 ≈ 54 H/s) does not
  change this (fixed difficulty, plan §4); other miners only cause rare
  stale work.
- Plan §4/§5 corrections: the plan guessed 5–20 H/s (from "0.43 s per hash"
  in the node's log); measured 4.2 H/s at 7 threads (≈ 1.06 s per hash per
  thread when one runs alone, ≈ 1.7 s with 7 running, memory-bandwidth
  bound). Plan §5's "4–6 threads is the likely sweet spot" is about right
  for efficiency, but more threads still add rate up to 8.
