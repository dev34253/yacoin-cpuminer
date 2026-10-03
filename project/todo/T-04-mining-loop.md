# T-04: Mining loop, threads and stats

- Depends on: T-02, T-03
- Size: M
- Owner:
- Started:
- Finished:

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
- [ ] Unit tests for nonce slicing, stale-work handling, target checks and the submit-retry logic.
- [ ] `--benchmark` runs without a node.

## Log
-
