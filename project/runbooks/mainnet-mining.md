# Runbook: mining on mainnet with yacoin-cpuminer

For the owner's laptop, where the mainnet node runs as `yacoind.service`
(RPC 127.0.0.1:7687, datadir `/srv/yacoin/datadir`, user `yacoin`). The miner
runs as your own user and talks to the node only over RPC.

Background (plan §4): difficulty is fixed at the minimum (`nBits 1e0fffff`,
about 1,048,577 hashes per block), so the time per block depends only on our
own hash rate. Reward 5.17 YAC per PoW block, spendable after 6 blocks.

## 1. Prerequisites (check every time before starting)

All checks below are read-only. `scripts/rpc-readonly.sh` reads the
credentials from the miner config and passes them to curl on stdin.

| Check | Command | Expect |
|---|---|---|
| Node running and synced | `scripts/rpc-readonly.sh getblockcount` | the network height (compare with a peer or explorer) |
| At least one peer | `scripts/rpc-readonly.sh getinfo` → `connections` | ≥ 1. Without a peer the node refuses `getwork` (fetch **and** submit, plan F8). The miner keeps a found block and retries, but a block found while the node is isolated may go stale. |
| Wallet unlocked | same `getinfo` → `unlocked_until` | absent (wallet not encrypted) or > 0. A locked wallet cannot sign found blocks (`getwork` error -100). |
| Keypool not empty | same `getinfo` → `keypoolsize` | > 0. Every `getwork` fetch reserves a key; an empty keypool with a locked wallet may crash the node (plan F12). Refill on the node with `keypoolrefill` if needed. |
| N-factor | `scripts/rpc-readonly.sh getmininginfo` → `Nfactor` | 21 |
| Wallet backed up | (owner) | a recent backup of `wallet.dat`; rewards go to the node's wallet |
| Memory | `free -g` | threads × 512 MiB + 1 GiB headroom available |

The miner repeats the N-factor, wallet, keypool, peer and memory checks at
start-up and refuses to start on a locked wallet, a wrong N-factor or too
little memory.

## 2. Credentials (owner decision Q3)

`~/.config/yacoin-cpuminer/miner.conf`, mode 600 in a mode-700 directory,
holds `rpchost`, `rpcport`, `rpcuser`, `rpcpassword` (copied from
`/srv/yacoin/datadir/yacoin.conf`) and `threads`. If the node's password
changes, update this file. Never put the password on a command line; the
miner refuses `--rpc-password` for that reason. Template:
`contrib/miner.conf.example`.

## 3. Start and stop

Build once (`scripts/build.sh`; all tests must pass). Then, in a terminal you
keep open (or `tmux`/`screen`):

```sh
cd ~/projects/cpu-miner
build/yacoin-cpuminer 2>&1 | tee -a ~/yacoin-cpuminer.log
```

Defaults: 7 threads (leaves one of the 8 hardware threads free, Q5; T-06
measurements in the README), nice 10, N-factor 21, huge pages requested.
Fewer threads: `--threads 4` (or `threads=` in miner.conf). The nice level
applies to all mining threads; `--nice 19` for the lowest priority.

Stop: Ctrl-C, or `pkill -INT yacoin-cpuminer` from another terminal. It
prints a final stats line. A solution being retried at that moment is lost
(it is logged as "stopping with an unsubmitted solution").

Optional systemd user unit (Q6: **not** started at boot for now; start it by
hand when wanted):

```ini
# ~/.config/systemd/user/yacoin-cpuminer.service
[Unit]
Description=yacoin-cpuminer (CPU miner for the local yacoind)

[Service]
ExecStart=%h/projects/cpu-miner/build/yacoin-cpuminer
Nice=10
KillSignal=SIGINT
Restart=on-failure
RestartSec=60
```

`systemctl --user daemon-reload`, then `systemctl --user start
yacoin-cpuminer` / `stop`; logs with `journalctl --user -u yacoin-cpuminer -f`.
Do not `enable` it unless the owner decides to start at boot.

## 4. What to watch

- The stats line every 60 s: `rate X H/s [per thread]`, `expected time per
  block`, and counters `work, found, accepted, rejected, stale, retried,
  dropped`. Rate should be stable; `rejected` should stay 0.
- `work` grows by about one per tip change plus one every 5 minutes. Each
  fetch adds ~15 lines to the node's `debug.log` and reserves a wallet key
  (plan F12); that is expected.
- Warnings about RPC errors: "Yacoin is not connected" (no peers) or
  "downloading blocks" (initial download) mean the node cannot take work or
  blocks right now; the miner waits and retries.
- The node: `systemctl status yacoind`, its memory (`ps -o rss,cmd -u
  yacoin`); the node needs a temporary 512 MiB for each submitted block.
- Laptop temperature: `sensors` (if `lm-sensors` is installed). Reduce
  `--threads` if it runs hot.

## 5. When a block is found

The miner logs:

```
FOUND block <hash> (nonce N, job J); submitting
ACCEPTED block <hash> on parent <prev>
```

Confirm (read-only):

1. Accepted by the node: the `ACCEPTED` line means `getwork <data>` returned
   true (or the node's tip already is our block). In the node's `debug.log`:
   `CheckWork () : new proof-of-work block found` followed by `hash: <hash>`.
2. In the main chain: `scripts/rpc-readonly.sh getblock '["<hash>"]'` →
   `confirmations` ≥ 1 and growing over the next blocks; `flags`
   `proof-of-work`. If `confirmations` is -1 or 0 later, the block was
   orphaned (another block won).
3. Coinbase in the wallet: the reward (≈ 5.17 YAC) goes to the node's wallet
   via a fresh key. It shows as immature and becomes spendable after 6
   confirmations (coinbase maturity after the fork, plan F13). Check with the
   wallet tools on the node (e.g. `getbalance`, `listtransactions`) as the
   node's operator.

## 6. When a submit is not accepted

| Miner log | Meaning | What to do |
|---|---|---|
| `STALE block … the tip moved before the submit` | Another block arrived first | Nothing; normal and rare. |
| `submit refused (… not connected …); keeping the solution and retrying` | The node has no peers (F8) | Check `getinfo` `connections`; the miner retries until accepted or the tip changes. |
| `submit: … wallet locked …` | Wallet locked (-100) | Unlock the node's wallet; the miner keeps retrying. |
| `node returned false for block …` | One of: no saved block (tip changed and templates were cleared), PoW not met, stale, or block rejected (F11) | Look in the node's `debug.log` around that time for `rpc getwork, No saved block`, `CheckWork () : … proof-of-work not meeting target`, `… generated block is stale`, or `ProcessBlock, block not accepted` and the validation error before it. A PoW failure would mean a miner bug: stop mining and report. |
| `HTTP 401` / `HTTP 403` | Wrong credentials / `rpcallowip` | Fix `miner.conf` (or the node's `rpcallowip`). |
| `the node reports Nfactor …` | `-nFactorAtHardfork` differs | Do not mine; check the node configuration. |
