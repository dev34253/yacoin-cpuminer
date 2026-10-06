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
- **Step 3: first mainnet run started 2026-10-03 13:32:27** (parent session,
  owner approved). Started detached so it outlives the Claude session:
  `setsid nohup build/yacoin-cpuminer >> ~/yacoin-cpuminer.log 2>&1 < /dev/null &`
  (PID in `~/.config/yacoin-cpuminer/miner.pid`, corrected by hand: `$!` held the setsid PID; stop with
  `kill $(cat ~/.config/yacoin-cpuminer/miner.pid)`). Pre-flight: height
  1,964,618, 2 connections, keypool 200, wallet not encrypted, Nfactor 21,
  20 GiB available. First stats after 60 s: **4.08 H/s** (7 threads,
  0.57–0.63 H/s each, AVX, huge pages, nice 10), expected 71 h per block;
  load average about 7. Supervision continues; T-07 stays in progress until
  the run has been watched for a few hours.
- **2026-10-04 06:45:20: owner chose 6 threads × 4 lanes (fused2)** in
  `~/.config/yacoin-cpuminer/miner.conf` (`threads=6`, `lanes=4`; the code
  defaults stay 7 × 2). Memory 12.50 GiB (about 8 GiB left available).
  First stats: 6.33 H/s, then 6.53 H/s, expected about 45 h per block
  (T-10 benchmark: 6.60 H/s). The previous 7 × 2 run ended at 6.00 H/s.
- **2026-10-04 07:00: power profile tried and reverted.** `powerprofilesctl set
  performance` (no sudo needed) gave no gain: the miner's effective clock,
  measured with `perf stat -e cycles,task-clock -p <pid>`, was 1.692 GHz under
  performance, 1.692 under balanced and 1.691 under performance again (A/B/A,
  20 s each, 6 × 4 fused2, about 60–65 °C). The 2.06 GHz all-core figure from
  T-08 was measured with the old 128-bit AVX code. The fused2 core uses
  256-bit AVX2, and the lower clock is most likely the CPU's AVX2 frequency
  offset, which the platform profile does not change. The hash rate stayed at
  about 6.6 H/s. Profile set back to `balanced`.
- **2026-10-05 04:19:42 EDT: first block found and accepted.** Block
  1,964,622, hash
  `00000098f01a02fd21aac1ebc8e2c485cfa66aac9ba54fe762f8f60e739725be`
  (nonce 2863311823), parent
  `000009fe7fb188a80cb8be93a08c95d642a5a62ef9b647085c94c9ff91aba483`.
  `getwork` returned true 4 s later. Checked 2026-10-05 22:08 EDT: it is the
  main-chain block at that height with 3 confirmations (blocks 1,964,623
  and 1,964,624 from other miners build on it), mint 5.167084 YAC, and the
  wallet lists it as `immature`, `generated: true` (spendable after 6
  confirmations). Mining stats at that point: 6 × 4 fused2, affinity spread,
  about 6.7 H/s, 821,208 hashes since the 11:33 restart on 2026-10-04,
  found 1, accepted 1, rejected 0, stale 0.
