# T-11: Smaller performance levers (each gated by measurements)

- Depends on: T-09 (T-10 if done)
- Size: S
- Owner: Claude (subagent)
- Started: 2026-10-04
- Finished: 2026-10-04

## Goal
Try cheap ideas one at a time on top of the best T-09/T-10 setting. Each one
runs only if its gate is met, and is kept only if it measures faster (plan
§12 method).

## Steps
1. **Thread pinning:**
   - `--affinity` option. Read the SMT sibling pairs from
     `/sys/devices/system/cpu/cpu*/topology/thread_siblings_list` (on this CPU
     they are (0,4), (1,5), (2,6), (3,7)); don't assume adjacent numbers are
     siblings.
   - Compare filling sibling pairs versus spreading across cores, leaving one
     logical CPU free. Watch for the node landing on a pinned CPU.
   - Expect little gain.
2. **1 GiB huge pages.** Gate: lanes ≥ 3 *and* T-08/T-09 show TLB walks
   (`dtlb_load_misses.walk_active`) are significant. The per-core STLB holds
   about 1536 entries; 2 MiB pages already cover L ≤ 2 for an SMT pair.
   Owner decision Q8, because it needs root and reserves memory away from
   the node even while the miner is off.
   - Runtime reservation usually fails on a long-running machine; the boot
     parameter `hugepagesz=1G hugepages=N` is the reliable way.
   - Use `mmap(MAP_HUGETLB | MAP_HUGE_1GB)` with a length that is a multiple
     of 1 GiB. Pack two 512 MiB tables per page and keep the extra ~256 B
     (X/Y) elsewhere. Fall back to 2 MiB THP.
   - `bench.sh` must read `Private_Hugetlb` / `HugePages_Free`, not
     `AnonHugePages`.
3. **Lookup gap** (store every k-th chunk, recompute the rest). Gate: more
   lanes are wanted but blocked by the 12 GiB budget. It adds about (k−1)/2
   dependent ChunkMix calls per random read (+25% work at k=2, +75% at k=4),
   and it doesn't reduce the number of random reads. Otherwise skip it.
4. For each lever tried: per-lane known-answer and differential tests still
   pass; update defaults, README and the example config; reviewer subagent;
   commit.

## Acceptance criteria
- [x] For each lever: either "gate not met" with the reason, or a benchmark
  table and a keep/drop decision.
- [x] Defaults change only for levers that measured faster than the spread.

## Log
- **Code** (WIP commit 6de8ff5 on a side branch, brought onto main by
  cherry-pick without conflicts, then reviewed and fixed):
  `--affinity none|compact|spread` / config key `affinity`. Each worker
  thread pins itself (`pthread_setaffinity_np`) before it allocates its
  tables; the coordinator, submitter and main threads are not pinned.
  - The SMT groups come from
    `/sys/devices/system/cpu/cpu*/topology/thread_siblings_list` (here
    (0,4) (1,5) (2,6) (3,7)), filtered to the online CPUs the process may
    use (`sched_getaffinity`, so taskset and cpusets are respected). Code is
    in `src/util/sysinfo.h` and `worker_cpus()` in `src/main.cpp`.
  - **compact:** both SMT threads of a core, core by core. Order 0 4 1 5 2 6
    3 7. With 6 threads that is CPUs 0 4 1 5 2 6: three full cores, and
    core 3 (CPUs 3 and 7) idle.
  - **spread:** the first SMT thread of every core, then the second ones.
    Order 0 1 2 3 4 5 6 7. With 6 threads that is CPUs 0–5: cores 0 and 1
    run two workers, cores 2 and 3 one each, and CPUs 6 and 7 stay free.
  - With 6 threads on 8 logical CPUs, two logical CPUs stay free in both
    modes (the task's "one free CPU" applies at 7 threads). At 7 threads,
    compact and spread pin the same CPU set (0–6, CPU 7 free); only the
    thread-to-CPU order differs.
  - With more threads than usable CPUs nothing is pinned, with a warning.
- **Tests:** `cpu_topology_orders` covers this laptop's layout, including
  the 6-thread prefixes. `cpu_topology_filters_and_parser_edges` covers the
  usable-CPU filter (taskset 0–5), inconsistent sibling lists, and empty or
  malformed lists. `affinity_option_parses` checks the default, a valid
  value and a rejected one. ctest passed 6/6 on the native build and on
  `-DYAC_NATIVE=OFF`.
- **Reviewer subagent** on the affinity diff. Applied:
  - the allowed CPU set (sched_getaffinity) is respected;
  - no silent wrap-around: with more threads than CPUs nothing is pinned;
  - the affinity mode appears in the `BENCH` line and the start-up lines;
  - offline CPUs and duplicate CPUs are filtered;
  - `parse_cpu_list` catches only `logic_error`, rejects junk, reversed
    ranges and CPUs above 1023 (the `CPU_SETSIZE` limit);
  - help text, `contrib/miner.conf.example` and README updated;
  - the `options.h` field order fixed;
  - tests added.

  Not applied:
  - `CPU_ALLOC` for more than 1024 CPUs (such CPUs are skipped instead);
  - one warning per failed pin (left as is: the usable-set filter removes
    the main cause).
- **Mainnet miner stopped 2026-10-04 10:43:50** (final stats 6.59 H/s,
  6 × 4). Restarted at 11:33:21 (see the deploy entry below).
- **Benchmark,** 2026-10-04 10:43:55–11:30:40:
  - Method (plan §12): `scripts/bench.sh`, 180 s per run, first 30 s
    discarded, 3 interleaved repeats, `build-dev` binary.
  - Machine state: AC on, governor powersave, EPP balance_performance, THP
    madvise; node idle (0–1 % CPU); package temperature 64–65 °C mean,
    66 °C max.
  - Clock: `perf stat -e cycles,task-clock -p` over 100 s inside each timed
    window, via a small wrapper. bench.sh's huge-page column then reads the
    wrapper's process, so it shows 0. The miner logs "huge pages on".
  - Pinning was checked in `/proc/<pid>/task/*/status`: the workers' masks
    were 0, 4, 1, 5, 2, 6 under compact; the main thread stayed 0–7.

| Config | Runs (H/s) | Median | Min–max | Spread | GHz (perf) | vs none |
|---|---|---|---|---|---|---|
| 6 × 4 fused2, none (live) | 6.615, 6.585, 6.618 | 6.615 | 6.585–6.618 | 0.5 % | 1.69 | – |
| 6 × 4 fused2, compact | 6.607, 6.642, 6.601 | 6.606 | 6.601–6.642 | 0.6 % | 1.90 | −0.1 % |
| **6 × 4 fused2, spread** | 6.676, 6.663, 6.667 | **6.667** | 6.663–6.676 | 0.2 % | 1.69 | **+0.8 %** |
| 7 × 2 fused2, none (code default) | 5.967, 5.971, 5.977 | 5.971 | 5.967–5.978 | 0.2 % | 1.69 | – |
| 7 × 2 fused2, spread | 5.969, 5.981, 5.970 | 5.970 | 5.969–5.981 | 0.2 % | 1.69 | 0.0 % |

  - **6 × 4, spread:** +0.8 % over none. Its lowest run (6.663) is above
    none's highest (6.618), and the gain is larger than none's 0.5 %
    spread, so it is real but small. Per thread, the two workers alone on
    a core (CPUs 2 and 3) run 1.30 H/s and the four that share a core run
    1.01 H/s.
  - **6 × 4, compact:** three full cores and one idle core give the same
    rate as none. The clock rises to 1.90 GHz with only 3 busy cores, which
    makes up for losing a core.
  - **7 × 2:** no difference (the pinned CPU set is the same as the
    kernel's choice).
- **Decision:**
  - The code default stays `affinity=none`, since nothing changes at the
    7 × 2 default.
  - For the owner's live 6 × 4, spread wins by more than the spread
    between runs, so `affinity=spread` was added to
    `~/.config/yacoin-cpuminer/miner.conf`. `threads=6` and `lanes=4` are
    unchanged.
- **Deployed 2026-10-04 11:33:21.** `build/` was rebuilt from this code and
  ctest passed 6/6.

  Start-up lines: "6 threads x 4 lanes (mix fused2, prefetch t0, affinity
  spread)", "affinity spread: worker CPUs 0 1 2 3 4 5", "new work 1". First
  stats line 11:34:23: 6.53 H/s [1.00 1.00 1.27 1.27 1.00 1.00]. Second line
  11:35:23: also 6.53 H/s. The live stats count whole 4-lane batches per
  60 s window, so they move in steps of about 0.07 H/s; the benchmark is the
  finer measure.

  Process note: an earlier `scripts/build.sh build-dev` call (missing
  `--build-dir`) had rebuilt `build/` at 10:39 while the old miner ran. The
  running process kept its old, unlinked binary and was not affected, and
  `build/` was rebuilt and tested again before the restart.
- **1 GiB huge pages: gate not met, not implemented.**
  - T-08 measured 0.0 % of cycles in TLB walks with 2 MiB THP at 1 lane.
  - On the live miner at 6 × 4 (the most memory per core so far),
    `perf stat -e cycles,dtlb_load_misses.walk_active,dtlb_store_misses.walk_active -p <pid> -- sleep 30`
    on 2026-10-04 11:35 gave load walks active in 1.58 % of cycles and
    store walks in 0.02 %. That is an upper bound for what 1 GiB pages
    could save; walk cycles partly overlap other work, so the real gain is
    less. It is not significant against the 0.2–0.6 % run spread plus the
    cost: root, a boot-time reservation taken from the node even while the
    miner is off (Q8), and a new allocation path.
  - Memory does not limit the rate either: a third lane at 7 threads
    (7 × 3 fused2, 10.5 GiB) adds only about 1.5 % over 7 × 2 (6.064 vs
    5.972 H/s, T-10). Hardware ALU throughput and the AVX2 clock (1.69 GHz)
    are the limit.
- **Lookup gap: gate not met, not implemented.** The gate is "more lanes
  are wanted but the 12 GiB budget blocks them". More lanes are not wanted:
  - the third lane at 7 threads adds only about 1.5 % (above);
  - 6 × 4 already fits the budget;
  - a gap of k = 2 would add about 25 % hashing work per random read to
    save memory that brings no rate.
