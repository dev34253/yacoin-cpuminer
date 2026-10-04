# T-11: Smaller performance levers (each gated by measurements)

- Depends on: T-09 (T-10 if done)
- Size: S
- Owner:
- Started:
- Finished:

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
- [ ] For each lever: either "gate not met" with the reason, or a benchmark
  table and a keep/drop decision.
- [ ] Defaults change only for levers that measured faster than the spread.

## Log
-
