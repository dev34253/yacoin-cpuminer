# T-08: Profile the hash and measure where the time goes

- Depends on: T-06
- Size: S
- Owner: Claude (subagent)
- Started: 2026-10-03
- Finished: 2026-10-03

## Goal
Replace the plan §12 estimates with numbers: compute versus memory share,
latency per random read, DRAM bandwidth, CPU clock and the memory setup, at
1, 4 and 7 threads. Decide from them what T-09/T-10 should target.

## Steps
1. **Cheap compute baseline (no sudo, no code):**
   - Run `--benchmark --threads 1 --nfactor 13`: a 2 MiB table, which fits
     in the 8 MiB L3.
   - Time per hash × 2^8 is the pure compute time per hash at N-factor 21;
     compare it with the N-factor 21 rate.
   - Repeat at 4 and 7 threads. The fill pass is the same work at any N, so
     this also gives the fill/read split.
2. Stop the mainnet miner and log the time (Q9). Record AC power, governor
   and EPP; follow the plan §12 measurement method (180 s, discard 30 s, 3
   repeats).
3. **Hardware counters** (owner decision Q7: `perf` with sudo). Under
   `perf stat` at 1, 4 and 7 threads, N-factor 21:
   - average L3-miss latency:
     `offcore_requests_outstanding.demand_data_rd / offcore_requests.demand_data_rd`;
   - stall share: `cycle_activity.stalls_l3_miss` with `cycles`, plus
     `perf stat --topdown` (or `-M TopdownL1`);
   - DRAM bandwidth: `uncore_imc/data_reads/` and `uncore_imc/data_writes/`;
   - effective clock: `cycles / task-clock`;
   - TLB: `dtlb_load_misses.walk_active`.

   Check names with `perf list` and use equivalents if one is missing.
4. **Memory setup:** `sudo dmidecode -t memory` (single or dual channel,
   speed). It is part of the same sudo decision (Q7).
5. Restart the mainnet miner and log the time.

## Acceptance criteria
- [x] A table in the Log, for 1, 4 and 7 threads, of: compute share, L3-miss
  stall share, average L3-miss latency (ns), DRAM read/write GB/s, effective
  GHz, TLB-walk share. Plus the DIMM configuration.
- [x] The resulting targets for T-09/T-10 written into plan §12. If the
  numbers contradict the plan (for example bandwidth-bound already), update
  §12 and the task order first.
- [x] No code change to the hash path (the cheap baseline uses existing options).

## Log
- Tooling (benchmark code only, not the hash path): `--bench-warmup SEC`
  (hashes finishing in the first SEC seconds are not counted; each thread's
  rate runs from its first to its last completion after the warm-up);
  `scripts/bench.sh` rewritten for the plan §12 method (configs
  `THREADS[:LANES[:ARGS]]`, interleaved repeats, median and min–max, AC /
  governor / EPP / THP header, clock and temperature sampling);
  `scripts/profile.sh` for the perf counters. Commit 07b5965 (WIP).
- Mainnet miner stopped 2026-10-03 23:07:29 (Q9; last stats line 4.07 H/s).
- Machine state for every run: AC online, governor `powersave` (intel_pstate
  active, HWP), EPP `balance_performance`, `platform_profile` balanced, THP
  `madvise`, no_turbo 0, RAPL limits PL1 56 W / PL2 78 W (readable; energy
  counters are root-only). Node idle (0 % CPU in every run).
- `perf` works without sudo (Q7: cap_perfmon). Notes: TopdownL1 events are
  not inherited by the miner's threads per process, so topdown and
  `uncore_imc` are counted system-wide over the same window (node idle);
  `offcore_requests_outstanding` must be in one event group with
  `offcore_requests` (ungrouped multiplexing gave a 2.5× lower ratio).
- **Memory setup:** `dmidecode` needs root (not available, Q7). Measured
  instead with a small probe (`scripts/membw.c`, 2 GiB, AVX2 loads, THP):
  sequential read peaks at **19.1 GB/s** (1 thread 16.5); random 128-byte
  chunk reads (16 in flight per thread) saturate at **11.5 GB/s =
  9.1·10⁷ chunks/s** from 2 threads on. 19 GB/s is the theoretical peak of
  one DDR4-2400 channel; dual channel would give about 30–35 GB/s. So the
  RAM almost certainly runs **single channel** (one DIMM or one populated
  channel). Unconfirmed without dmidecode.

### Step 1: compute baseline (the table fits in cache)
`scripts/bench.sh --seconds 45 --warmup 10`, 3 interleaved repeats
(N-factor 13: 2 repeats). Compute time per hash at N-factor 21 =
(1 / per-thread rate) × 2^(21−nf). N-factor 10 = 256 KiB per thread (two
per core still fit the 1 MiB L2), so its random reads are L2 hits.

| N-factor | Threads | Median H/s | Min–max | Per thread | → compute s/hash at NF21 |
|---|---|---|---|---|---|
| 10 | 1 | 2534.6 | 2513.2–2542.4 | 2534.6 | 0.81 |
| 10 | 4 | 6190.0 | 6184.7–6204.1 | 1547.5 | 1.32 |
| 10 | 7 | 8598.2 | 8583.3–8601.2 | 1228.3 | 1.67 |
| 10 | 8 | 9350.4 | 9330.2–9358.2 | 1168.8 | 1.75 |
| 13 | 1 | 323.1 | 320.0–326.2 | 323.1 | 0.79 |
| 13 | 4 | 829.5 | 826.2–832.8 | 207.4 | 1.23 |

### Steps 2–3: N-factor 21, 180 s per run, first 30 s discarded
Rates: two `bench.sh` repeats plus the `profile.sh` run (third repeat, under
perf) per thread count, interleaved 1/4/7, 1/4/7, 1/4/7.

| Threads | Runs (H/s) | Median | Per thread | Compute s/hash (step 1) | Measured s/hash | **Compute share** |
|---|---|---|---|---|---|---|
| 1 | 0.960, 0.965, 0.881* | 0.960 | 0.960 | 0.81 | 1.04 | **78 %** |
| 4 | 2.777, 2.785, 2.760 | 2.777 | 0.694 | 1.32 | 1.44 | **92 %** |
| 7 | 4.168, 4.170, 4.123 | 4.168 | 0.595 | 1.67 | 1.68 | **~100 %** |

\* perf run; the system-wide topdown/imc perf costs a few % at 1 thread.

Hardware counters (perf, 150 s window after the warm-up; read/write GB/s
system-wide from `uncore_imc`, 64 B per count):

| Threads | Effective GHz (cycles/task-clock) | L3-miss stall share (stalls_l3_miss/cycles) | Avg L2-miss demand-read latency | DRAM read GB/s | DRAM write GB/s | TLB-walk share (walk_active/cycles) | TopdownL1 (system-wide) |
|---|---|---|---|---|---|---|---|
| 1 | **3.04** | 28 % | 73 ns | 1.09 | 0.48 | 0.0 % | retiring 27 %, backend 68 %, frontend 4 % |
| 4 | **2.07** | 22 % | 84 ns | 3.32 | 1.48 | 0.0 % | retiring 30 %, backend 69 %, frontend 1 % |
| 7 | **2.06** | 21 % | 93 ns | 5.24 | 2.22 | 0.0 % | retiring 45 %, backend 42 %, frontend 13 % |

### What the numbers say
1. **The clock, not memory, explains most of the per-thread drop.** One busy
   core runs at 3.0 GHz; with 4 or more busy cores the clock is held at
   **2.06 GHz** although the package is cool (≤ 60 °C) and far below PL1
   (56 W). That is a firmware/platform cap (platform_profile `balanced`,
   EPP `balance_performance`), not thermal or power throttling. Compute per
   hash scales with it: 0.81 s at 3.0 GHz → 1.32 s at 2.07 GHz (×1.6 for
   ×1.47 clock; the rest is the shared L2 and uncore).
2. **At the operating point (7 threads) the miner is compute-bound.** The
   time per hash with a 512 MiB table equals the time with an L2-resident
   table: the SMT sibling already hides the DRAM wait completely (the 21 %
   L3-miss stall cycles of one thread are used by the other). Plan §12's
   estimate ("compute is about half the time") holds only for 1 thread
   (78 % compute there, not 50 %).
3. **Memory has headroom.** At 4.17 H/s the DRAM moves 7.5 GB/s (5.2 read +
   2.2 write), well under the single channel's ~19 GB/s sequential and
   ~11.5 GB/s random-chunk ceilings; average loaded latency 73–93 ns.
   Rough bandwidth ceiling for this hash (512 MiB random reads + 512 MiB
   fill writes with their RFO reads per hash): about 8–10 H/s.
4. **TLB walks are negligible** (0.0 %) with 2 MiB THP: 1 GiB pages (T-11)
   have nothing to gain at L = 1.
5. **ALU throughput is the limit at 7 threads:** retiring 45 % of the 4-wide
   slots ≈ 1.8 µops/cycle per core, nearly all 128-bit vector ops on ports
   0/1/5 (ChaCha's shuffles all go to port 5). A 256-bit fused 2-lane mix
   does two lanes' work per µop.

### Targets for T-09/T-10 (written into plan §12)
- **T-09 (lanes + prefetch, plain mix)** can only help where DRAM wait is
  not yet hidden: 1–4 threads (8–22 % memory time). Expect **≈ 0 % at 7–8
  threads**. It is still needed as the frame for T-10 (fused pairs need two
  lanes per thread).
- **T-10 (fused AVX2 pairs)** attacks the real limit (vector µops per hash).
  Potential up to ~1.5–2× compute throughput, until the memory ceiling
  (~8–10 H/s). This is now the main lever. Fewer threads with more lanes
  (e.g. 4 × 2 fused) may match 7 × 1 at half the threads.
- **Not in software:** the 2.06 GHz all-core cap. With the `performance`
  platform profile the all-core clock might rise (owner decision; not
  changed here, recorded as a question).
- Task order unchanged (T-10 builds on T-09's lane loop), but plan §12's
  expectations are updated: T-09 alone ≈ 0 at 7 threads; T-10 is the win.
