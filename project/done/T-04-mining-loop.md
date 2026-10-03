# T-04: Mining loop, threads and stats

- Depends on: T-02, T-03
- Size: M
- Owner: Claude (subagent)
- Started: 2026-10-03
- Finished: 2026-10-03

## Goal
A working miner: threads scan nonces, solutions are submitted, work stays fresh.

## Steps
1. Worker threads with their own nonce slices and their own scratch buffer; check each hash against the target.
2. Coordinator: poll `getbestblockhash` every few seconds; call `getwork` only on a tip change, after a submit, or every few minutes (configurable, well under the node's 5400 s template age) to limit `debug.log` noise and keypool use (F9, F12). Drop old work at once on a tip change.
3. Submit: check the hash against the target locally first; on "no connections"/"initial download" keep the solution and retry until accepted or the tip changes; "stale" = tip changed before the submit; log other `false` results with a pointer to the node's `debug.log` (F11).
4. Start-up checks: N-factor from `getmininginfo` matches `--nfactor`; memory (threads × scratch size, plus headroom for the node) against available RAM; wallet keypool not empty.
5. Options: `--threads`, `--nice`, `--tip-poll`, `--work-refresh`, `--rpc-*`, `--conf`, `--nfactor`, `--benchmark`.
6. Stats every N seconds and on exit: H/s per thread and total, work fetched, found, accepted, rejected, stale, retried.
7. Clean shutdown on SIGINT/SIGTERM.

## Acceptance criteria
- [x] Unit tests for nonce slicing, stale-work handling, target checks and the submit-retry logic.
- [x] `--benchmark` runs without a node.

## Log
- `src/miner/miner.{h,cpp}`: `JobBoard` (current job + generation counter;
  workers check it before every hash), workers with their own nonce slice
  (`nonce_slice`) and 512 MiB `ScryptHasher`, a submitter thread
  (`Submitter`: local target check → tip check → `getwork <data>` → classify),
  and the coordinator (`Miner::run`: `getbestblockhash` every `--tip-poll`,
  `getwork` only on tip change / no work / `--work-refresh` / forced refresh,
  stats every `--stats-interval`). `run_benchmark` for `--benchmark`.
- `src/main.cpp`: options, nice (set before threads start, inherited),
  SIGINT/SIGTERM (second signal exits at once), start-up checks (node
  Nfactor == `--nfactor`, wallet locked → refuse, encrypted wallet with empty
  keypool → refuse, timed unlock → warning, no peers → warning, memory
  threads × scratch + 1 GiB ≤ MemAvailable), `--check-work`, `--benchmark`.
- Unit tests `tests/test_miner.cpp` (17 cases) with `tests/node_sim.h`, an
  in-process stand-in for the node's getwork (saves work by merkle root,
  takes only time+nonce on submit, recomputes the hash, checks target and
  tip): nonce slicing; Submitter accepted / not sent on local check failure /
  stale before submit / retry -9,-10 then accepted / retry ended by tip
  change / wallet locked / false → rejected, stale or accepted (own hash is
  tip) / stop during retry; Miner finds and submits 5 blocks; drops work on
  an external block with exactly one new getwork; waits for peers; retries
  refused submits; getwork not repeated on an unchanged tip (F12); recovers
  from a node restart ("No saved block"); retries while node unreachable;
  benchmark. 5 repeated runs all pass.
- First live run on the test chain: 5 blocks found and accepted (2 threads,
  ~40k H/s at N-factor 4). Manual checks: `--nfactor 21` against the test
  chain is refused ("node reports Nfactor 4"); `--threads 64 --benchmark`
  at N-factor 21 is refused by the memory check (32 GiB > 20 GiB available).
- **Review** (reviewer subagent; it also ran test_miner under ThreadSanitizer
  4× with no reports). Applied:
  - B1 node restart → stale saved blocks: forced fetch after an RPC error
    recovery and after a rejected submit (test added).
  - B2 timed wallet unlock / empty keypool: refuse to start with an encrypted
    wallet and empty keypool, warn on a timed unlock, and check `getinfo`
    before every getwork: no fetch while locked with an empty keypool (F12).
  - R1 no extra getwork after accept/stale when work on the new tip exists
    (soft vs forced refresh; test: exactly one fetch per tip change, one fetch
    over 25 polls with no change).
  - R2 the locked-wallet error is logged once per solution.
  - R3 second SIGINT/SIGTERM exits immediately.
  - R4 documented in the README (up to one tip-poll interval of hashing on a
    dead tip; such solutions are not submitted).
  - Nits: removed unused `invalid_before_job_`; `submitted` shown in stats;
    workers check `stop` before allocating 512 MiB.
  - Not applied: `dead_prev` keeps one parent only (harmless: older ones are
    classified stale with one cheap RPC); an accepted block overtaken before
    the first tip check is counted stale (cosmetic, rare); unit tests for
    `memory_ok` and the N-factor refusal (they live in `main.cpp`; checked
    manually above instead).
