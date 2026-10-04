# Open questions for the owner

## Answers (owner, 2026-10-03)

- **Q1:** first wait; then (2026-10-04) a public repository on dev34253:
  https://github.com/dev34253/yacoin-cpuminer.
- **Q2:** go ahead with implementation.
- **Q3:** option (c): RPC credentials in a separate miner config file,
  `~/.config/yacoin-cpuminer/miner.conf` (mode 600, directory 700), copied
  from the node's `yacoin.conf`. Done; credentials checked with
  `getblockcount`.
- **Q4:** licence not important; MIT is used.
- **Q5:** leave one core free: default 7 threads on this 8-thread laptop
  (7 × 512 MiB = 3.5 GiB of scratch memory).
- **Q6:** do not start at boot for now; run by hand.

## Questions as asked

- **Q1 – Repository:** Create a GitHub repo for this project? Under which
  account, public or private, and what name
  (suggestion: `yacoin-cpuminer`)? Until then the repo is local only, in
  `~/projects/cpu-miner`.
- **Q2 – Go ahead:** Should implementation start after the P0-08 task in the
  yacoin project is finished? Expected yield is small: difficulty is fixed
  at the minimum, so it depends only on our own hash rate, roughly one block
  per day or two at 10 H/s (5.17 YAC each; plan §4). Is it still worth it as
  a hobby and testing tool?
- **Q3 – RPC credentials:** The node's RPC password is in
  `/srv/yacoin/datadir/yacoin.conf`, readable only by the `yacoin` user.
  Options: (a) run the miner as `yacoin` and read that file; (b) add a second
  RPC user for the miner with `rpcauth=` in `yacoin.conf` (needs a node
  restart); (c) a copy of the password in a miner config file only you can
  read. Suggestion: (a) to start, (b) later.
- **Q4 – Licence** for the miner's own code. Suggestion: MIT, same as yacoin
  (the copied code stays under its own MIT/public-domain notices).
- **Q5 – CPU budget:** How many threads may it use by default while the node
  runs and the laptop is also used for builds? Suggestion: 4 threads (2 GiB
  RAM), `nice 10`, and you can change it at any time.
- **Q6 – Always on?** Run it as a systemd service that starts at boot, or
  only by hand?

## Phase 2 answers (owner, 2026-10-03)

- **Q7:** option 3, `cap_perfmon` on `/usr/bin/perf` (set by the owner;
  `getcap` shows `cap_perfmon=ep`). `perf` with per-process and `uncore_imc`
  counters now works without sudo. Remove with `sudo setcap -r /usr/bin/perf`;
  it is lost when the `linux-perf` package updates. `dmidecode` and RAPL still
  need root (not used unless the owner runs them).
- **Q9:** yes, test as many changes as needed; the mainnet miner may be
  stopped for benchmarks and must be restarted afterwards.
- **Q8:** still open (decide only if T-11's gate is met).

## Phase 2 (performance) questions

- **Q7 – sudo for profiling (T-08):** `kernel.perf_event_paranoid` is 4,
  which blocks user-space hardware counters, and the DRAM counters
  (`uncore_imc`), `dmidecode` (memory channels) and RAPL energy are root-only.
  Options: (a) run `perf` and `dmidecode` with sudo for the profiling runs
  only; (b) `sudo sysctl kernel.perf_event_paranoid=1` for the session and
  restore 4 afterwards. Suggestion: (a).
- **Q8 – 1 GiB huge pages (T-11):** reserving them reliably needs a boot
  parameter (root) and locks that memory away from other programs (including
  the node) even while the miner is off. Try it at all? Suggestion: only if
  T-09 ends up with 3+ lanes and the profile shows significant TLB walks.
- **Q9 – Pausing mainnet mining for benchmarks:** the benchmarks need the
  running miner stopped for their duration (about 1.5–2 hours for T-09 with
  repeats, less for the others). OK to stop and restart it as part of T-08 to T-11? Suggestion: yes,
  logged in each task.

