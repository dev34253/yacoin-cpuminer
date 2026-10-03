# T-07: Mainnet run and runbook

- Depends on: T-05, T-06, owner answers to Q2, Q3, Q5, Q6
- Size: S
- Owner: Claude (subagent) for steps 1–2; step 3 (first supervised run) by the parent session/owner
- Started: 2026-10-03
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
- Steps 1–2 done: `project/runbooks/mainnet-mining.md` (prerequisites with
  read-only checks via `scripts/rpc-readonly.sh`, credentials, start/stop,
  nice/threads, optional systemd user unit not enabled (Q6), what to watch,
  confirming a found block, what a `false` submit means and where to look in
  `debug.log`). Template config `contrib/miner.conf.example`.
- State at hand-over (read-only checks, 2026-10-03): node height 1,964,617,
  1 connection, keypool 200, wallet not encrypted, Nfactor 21. The one
  `--check-work` getwork (T-03) decoded to a sane header.
- Step 3 (first supervised mainnet run) is NOT done; left for the parent
  session. Command: `cd ~/projects/cpu-miner && build/yacoin-cpuminer 2>&1 | tee -a ~/yacoin-cpuminer.log`.
- **Docs review** (reviewer subagent over README, runbook, CLAUDE.md, example
  config, task logs; checked log strings and node behaviour against the
  code). Applied: `submitted` was missing from the stats line (now printed;
  this also makes the T-04 log statement true); `| tee` + Ctrl-C lost the
  final stats → miner ignores SIGPIPE and the runbook uses `tee -i`;
  systemd unit now `Restart=no` (refusals would loop); a no-echo recipe to
  copy the credentials; orphaned blocks show `confirmations` -1 (not 0);
  `flags` "starts with" proof-of-work; the miner's `false` message names the
  real debug.log strings (`ERROR: CheckWork () : ProcessBlock, block not
  accepted`); runbook §6 rows for `submit failed`, `BUG:` and the other
  transient refusals; `--help` lists `rpctimeout`; `$XDG_CONFIG_HOME`
  documented and honoured by `rpc-readonly.sh` (which also trims CR now);
  inline comments not allowed in miner.conf (documented); CLAUDE.md lists
  all allowed read-only RPCs and points to the runbook; key reservation
  worded per `getwork` call. Not applied: nothing.
- Note: the first runbook draft was committed together with T-05 (d04e463)
  by a broad `git add project`; this commit carries the reviewed version.
