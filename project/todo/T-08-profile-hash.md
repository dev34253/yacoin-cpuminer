# T-08: Profile the hash and measure where the time goes

- Depends on: T-06
- Size: S
- Owner:
- Started:
- Finished:

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
- [ ] A table in the Log, for 1, 4 and 7 threads, of: compute share, L3-miss
  stall share, average L3-miss latency (ns), DRAM read/write GB/s, effective
  GHz, TLB-walk share. Plus the DIMM configuration.
- [ ] The resulting targets for T-09/T-10 written into plan §12. If the
  numbers contradict the plan (for example bandwidth-bound already), update
  §12 and the task order first.
- [ ] No code change to the hash path (the cheap baseline uses existing options).

## Log
-
