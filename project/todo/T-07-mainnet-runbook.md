# T-07: Mainnet run and runbook

- Depends on: T-05, T-06, owner answers to Q2, Q3, Q5, Q6
- Size: S
- Owner:
- Started:
- Finished:

## Goal
Run the miner against the mainnet node safely, with a runbook.

## Steps
1. Runbook: prerequisites (node synced, at least one peer, wallet unlocked and backed up, keypool not empty), credentials (Q3), start/stop, nice/threads, optional systemd unit (Q6), logs and stats, where to look in the node's `debug.log` when a submit returns `false`.
2. How to confirm a found block: accepted by `getwork`, in the main chain (`getblock` confirmations grow), coinbase in the wallet (spendable after 6 blocks, F13).
3. First supervised run of a few hours; record H/s, effect on the node, and any submits.

## Acceptance criteria
- [ ] Runbook committed; first run recorded in the log.

## Log
-
