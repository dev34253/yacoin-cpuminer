# T-05: Integration test on a private low-difficulty chain

- Depends on: T-04, T-05a
- Size: M
- Owner: Claude (subagent)
- Started: 2026-10-03
- Finished: 2026-10-03

## Goal
Show end to end that blocks found by the miner are accepted by yacoind (plan §7.4).

## Steps
1. Use the T-05a harness (two linked low-difficulty nodes, version-7 work, N-factor 4).
2. Run the miner against node 1; expect `getblockcount` to rise on both nodes.
3. Prove the blocks are the miner's: match the miner's accepted submits with the new block hashes (and the node's `CheckWork` lines in `debug.log`).
4. Stale work: let node 2 mine a block (`setgenerate` or `generatetoaddress`) while the miner works; the miner must drop the old work.
5. Submit retry (plan §7.5): stop node 2, let the miner find a block (submit refused, no connections), restart node 2, the retry is accepted.
6. Never touch the mainnet node or `/srv/yacoin`.

## Acceptance criteria
- [x] Script finds and has accepted at least 5 blocks, repeatably; stale and retry cases pass.
- [x] Documented how to run it.

## Log
- `tests/integration.sh` (documented in the README): fresh two-node chain
  (T-05a harness), then
  - A. two separate miner runs with `--max-blocks 5`: every ACCEPTED hash is
    in both nodes' main chains (`getblock` confirmations ≥ 1) and has a
    `CheckWork … hash: <hash>` line in node 1's `debug.log`; no rejected
    submits;
  - ramp: 70 more blocks (difficulty rises 4× per 10 blocks with
    `epochinterval=10`; at height 80 nBits `1e0bfff0`, tens of seconds per
    block for one thread);
  - B. stale: while the miner works, node 2 mines a block with its own
    `generatetoaddress`; the miner logs "tip changed …; dropped old work" and
    gets work on node 2's block;
  - C. retry (plan §7.5): node 2 stopped → node 1 has 0 peers; the miner finds
    a block, the submit is refused (-9 "Yacoin is not connected!") and kept;
    node 2 restarted; the retried submit is accepted and the block is in both
    chains; retry counter > 0; SIGINT gives a clean exit.
- Results: 3 full runs, all `INTEGRATION TEST PASSED` (14/14 checks each;
  runs 2 and 3 with the final T-04 code). Per run: 10 blocks in phase A +
  70 ramp + 1–3 in phase C, all accepted, 0 rejected. Phase C: retried 3–4
  times (every 2 s) and then accepted, e.g. block
  `000002c38c83e4f56916ee82ddea4e1c5b5308cbd12a83f3ceb2663f8bbbb8ee`.
  Total ≈ 2 min per run on this laptop.
- No forced stale *submit* happens on the live chain (the miner drops old
  work before it finds a block on it); the stale submit path
  (tip moved before / during submit) is covered by `test_miner`.
- Never touched the mainnet node: the miner gets node 1's config via `--conf`.
