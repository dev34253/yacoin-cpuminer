# yacoin-cpuminer

A small, standalone CPU miner for Yacoin proof-of-work blocks. It gets work
from a local `yacoind` over JSON-RPC (`getwork`) and hands solved blocks back.
The node builds the block, signs it and pays its own wallet; the miner only
searches nonces.

- PoW hash: the node's scrypt-jane (Keccak-512 + ChaCha20/8, N = 2^(Nfactor+1),
  r = p = 1) over the 84-byte version-7 header. At mainnet's N-factor 21 every
  hash needs **512 MiB** of memory, so each thread allocates 512 MiB once.
- Proven against real mainnet blocks (known-answer tests) and a private test
  chain (integration test). Plan and facts: `project/plans/plan.md`.

## Build

Requirements: Linux x86-64, gcc/g++ (C++17), CMake ≥ 3.16, libcurl development
headers.

```sh
sudo apt install build-essential cmake libcurl4-openssl-dev
scripts/build.sh            # configure + build (nice 10, -j4) + ctest, in build/
```

The binary is `build/yacoin-cpuminer`. `scripts/build.sh` falls back to `uvx
--from cmake` and a locally unpacked `libcurl4-openssl-dev` (no sudo) when
cmake or the curl headers are missing.

CMake option `-DYAC_NATIVE=OFF` builds with the node's baseline `-msse2`
(ChaCha SSE2 code) instead of `-march=native` (AVX on this laptop), e.g.
`scripts/build.sh --build-dir build-sse2 -DYAC_NATIVE=OFF`.

## Configure

The miner reads **`~/.config/yacoin-cpuminer/miner.conf`** by default
(`$XDG_CONFIG_HOME/yacoin-cpuminer/miner.conf` if that is set; another file
with `--conf FILE`). It holds a copy of the node's RPC credentials, so keep
it private:

```sh
mkdir -p ~/.config/yacoin-cpuminer && chmod 700 ~/.config/yacoin-cpuminer
cp contrib/miner.conf.example ~/.config/yacoin-cpuminer/miner.conf
chmod 600 ~/.config/yacoin-cpuminer/miner.conf   # then fill in rpcuser/rpcpassword/rpcport
```

Keys: `rpchost`, `rpcport`, `rpcuser`, `rpcpassword`, `rpctimeout`, `threads`,
`nice`, `nfactor`, `tip_poll`, `work_refresh`, `retry`, `stats_interval`,
`hugepages`. A `#` starts a comment only at the start of a line.
Command-line options override the file. The password is
deliberately **not** accepted on the command line (it would show in `ps`).
`--yacoin-conf FILE` reads `rpcuser`/`rpcpassword`/`rpcport`/`rpcconnect` from
a node's `yacoin.conf` instead. The miner warns if a config file is readable
by group or others.

The node needs: at least one peer (otherwise `getwork` is refused), an
unlocked (or unencrypted) wallet, and a non-empty keypool. The miner checks
these at start-up, and that the node's `Nfactor` equals `--nfactor` (21).

## Run

```sh
build/yacoin-cpuminer                      # defaults: 7 threads, nice 10, N-factor 21
build/yacoin-cpuminer --threads 4          # fewer threads
build/yacoin-cpuminer --check-work         # one getwork, decode + sanity checks, no mining
build/yacoin-cpuminer --help
```

Stop with Ctrl-C (SIGINT) or SIGTERM; it prints final stats (with `| tee`,
use `tee -i` so the final lines are kept). Every
`--stats-interval` seconds (60) it logs H/s (total and per thread), the
expected time per block, and counters: work fetched, found, submitted, accepted,
rejected, stale, retried, dropped.

How it works: one coordinator thread polls `getbestblockhash` every
`--tip-poll` s (5) and calls `getwork` only on a tip change, after a submit,
or every `--work-refresh` s (300) — each `getwork` fetch makes the node save a
block template, reserve a wallet key and write ~15 lines to `debug.log`. Each
worker scans its own slice of the 32-bit nonce space and drops its work as soon
as the tip changes (noticed within one tip poll; a solution on an old tip
found in that window is recognised as stale and not submitted). Before each
`getwork` the miner checks the node's wallet: it never fetches while the
wallet is locked and the keypool is empty (that could crash the node). A found block is checked against the target locally, then
submitted with the node's timestamp unchanged. If the node refuses because it
has no peers or is in initial download, the solution is kept and retried
every `--retry` s until it is accepted or the tip changes. A plain `false`
from the node is logged with a pointer to its `debug.log`, and new work is
fetched (the node may have restarted and lost its saved blocks). A second
Ctrl-C exits at once.

### Running in the background

The miner keeps running only as long as the process that started it, unless
it is detached. Three ways, all reading `~/.config/yacoin-cpuminer/miner.conf`:

1. **`screen`** (installed; `tmux` is not). Watch it live and detach:
   ```sh
   screen -S miner
   cd ~/projects/cpu-miner && build/yacoin-cpuminer 2>&1 | tee -i -a ~/yacoin-cpuminer.log
   # detach: Ctrl-A, then D. Reattach (also over SSH): screen -r miner. Stop: Ctrl-C.
   ```
2. **Transient systemd user service** (no unit file; not started at boot):
   ```sh
   systemd-run --user --unit=yacoin-cpuminer -p KillSignal=SIGINT \
     --working-directory=%h/projects/cpu-miner %h/projects/cpu-miner/build/yacoin-cpuminer
   systemctl --user status yacoin-cpuminer         # status
   journalctl --user -u yacoin-cpuminer -f         # log
   systemctl --user stop yacoin-cpuminer           # stop (prints final stats)
   ```
   With user lingering off (the default), it stops when you log out of the
   laptop completely.
3. **Detached with `nohup`** (survives closing the terminal):
   ```sh
   cd ~/projects/cpu-miner
   setsid nohup build/yacoin-cpuminer >> ~/yacoin-cpuminer.log 2>&1 < /dev/null &
   tail -f ~/yacoin-cpuminer.log                   # watch
   pkill -TERM -x yacoin-cpuminer                  # stop (prints final stats)
   ```
   (Don't use `$!` for the PID here: in an interactive shell `setsid` forks,
   so `$!` is the short-lived `setsid` process, not the miner.)

Is it running? `pgrep -af yacoin-cpuminer`. None of these start it after a
reboot.

Mainnet procedure: `project/runbooks/mainnet-mining.md`.

## Benchmark

No node needed:

```sh
build/yacoin-cpuminer --benchmark --threads 4 --bench-seconds 90
build/yacoin-cpuminer --benchmark --threads 2 --nfactor 4       # test-chain N-factor
```

Results on this laptop: see "Performance" below.

## Tests

```sh
scripts/build.sh                 # unit tests incl. known-answer tests (ctest)
tests/integration.sh             # end-to-end on a private test chain (~5-15 min)
```

- `test_hash`: the scrypt hash at N-factor 21 of five real mainnet version-7
  headers (heights 1,890,000 – 1,964,617) equals their block hashes; three
  test-chain blocks at N-factor 4 likewise.
- `test_getwork`: real `getwork` replies from the test chain decode to
  exactly the header the node logged (`raw_block_header_hex`); encoding a
  nonce changes only the nonce word.
- `test_target`, `test_rpc`, `test_miner` (nonce slicing, stale work,
  submit/retry logic, an in-process node stand-in), `test_util`.
- `tests/integration.sh`: two linked low-difficulty nodes; the miner's blocks
  are accepted by both; stale work after another node's block; a submit
  refused while the node has no peers is retried and accepted.

## Private test chain

`tests/testchain.sh` runs two low-difficulty yacoind nodes (N-factor 4,
version-7 blocks from height 0) linked only to each other on loopback ports
27688/27689 (P2P) and 27687/27690 (RPC). It needs a yacoind built with
`--enable-low-difficulty-for-development` (yacoin `contrib/testing/build.sh
--config lowdiff`):

```sh
tests/testchain.sh install ~/path/to/build-lowdiff/src   # copies into testchain/bin (git-ignored)
tests/testchain.sh start      # fresh datadirs in testchain/data, waits until connected
tests/testchain.sh status
tests/testchain.sh cli 1 getmininginfo
build/yacoin-cpuminer --conf "$(tests/testchain.sh conf 1)" --nfactor 4 --threads 2
tests/testchain.sh stop
```

The low-difficulty build keeps mainnet's magic bytes and port, so the script
isolates the nodes (`-connect` to each other only, `-bind=127.0.0.1`,
`-dnsseed=0`, `-discover=0`) and refuses to start if a port is in use or the
binary is not a low-difficulty build.

## Performance

Measured 2026-10-03 on this laptop (Intel i5-8300H, 4 cores / 8 threads,
30 GiB RAM, THP mode `madvise`) at N-factor 21, `nice 10`, 90 s per run after
a warm-up hash, with the mainnet node running alongside (idle: 0–4 % CPU
during the runs). `scripts/bench.sh` reproduces the miner rows,
`scripts/builtin-bench.sh` the built-in-miner rows.

| Build | Threads | Huge pages | Total H/s | Per thread H/s | Max pkg temp | Expected time per block* |
|---|---|---|---|---|---|---|
| AVX (`-march=native`) | 1 | on | 0.945 | 0.945 | 49 °C | 308 h |
| AVX | 2 | on | 1.672 | 0.84 | 49 °C | 174 h |
| AVX | 3 | on | 2.224 | 0.74 | 52 °C | 131 h |
| AVX | 4 | on | 2.788 | 0.70 | 52 °C | 104 h |
| AVX | 5 | on | 3.237 | 0.58–0.69 | 54 °C | 90 h |
| AVX | 6 | on | 3.697 | 0.58–0.69 | 55 °C | 79 h |
| **AVX** | **7** | **on** | **4.152** | 0.58–0.67 | 56 °C | **70 h (2.9 days)** |
| AVX | 8 | on | 4.584 | 0.57 | 57 °C | 64 h |
| AVX | 1 | off | 0.863 | 0.863 | 56 °C | 338 h |
| AVX | 4 | off | 2.372 | 0.59 | 54 °C | 123 h |
| AVX | 7 | off | 3.487 | 0.49–0.52 | 56 °C | 84 h |
| SSE2 (`-msse2`, node baseline) | 1 | on | 0.859 | 0.859 | 55 °C | 339 h |
| SSE2 | 4 | on | 2.535 | 0.63 | 54 °C | 115 h |
| SSE2 | 7 | on | 3.795 | 0.53 | 56 °C | 77 h |
| yacoind built-in miner (`setgenerate`, test node at N-factor 21) | 1 | – | 0.64 | 0.64 | – | 455 h |
| yacoind built-in miner | 4 | – | 1.58 | 0.40 | – | 184 h |

\* 1,048,577 hashes per block at mainnet's fixed minimum difficulty `1e0fffff` (plan §4) ÷ rate.

- **Default: 7 threads** (owner decision Q5, confirmed): 4.15 H/s, about
  70 hours per block on average (blocks arrive randomly: some much sooner,
  some much later). The 8th thread would add 10 % (4.58 H/s) but takes the
  last free hardware thread. With 4 threads (2.79 H/s, 104 h/block) the
  miner uses half the CPU for two thirds of the rate; a good choice while
  the laptop is busy.
- **Huge pages on by default**: +9 % at 1 thread, +19 % at 7 threads.
- **`-march=native` (AVX ChaCha) by default**: +10 % over the node's SSE2.
- **vs the node's built-in miner**: +47 % at 1 thread, +76 % at 4 threads
  (scratch buffer reused instead of 512 MiB malloc/free per hash, AVX, huge
  pages).
- Memory: 512 MiB per thread (7 threads = 3.5 GiB); the start-up check
  wants that plus 1 GiB free. Temperatures stayed below 60 °C.

## Licence

MIT (`LICENSE`). Copied code in `third_party/` keeps its own licence; see the
`SOURCE.md` files there.
