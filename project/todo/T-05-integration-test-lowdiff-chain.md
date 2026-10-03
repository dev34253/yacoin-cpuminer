# T-05: Integration test on a private low-difficulty chain

- Depends on: T-04, T-05a
- Size: M
- Owner:
- Started:
- Finished:

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
- [ ] Script finds and has accepted at least 5 blocks, repeatably; stale and retry cases pass.
- [ ] Documented how to run it.

## Log
-
