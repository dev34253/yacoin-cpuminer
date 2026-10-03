# Plan: standalone Yacoin CPU miner

Status: draft for owner review, 2026-10-03. Reviewed by an independent
subagent the same day; its findings are worked in (see §11).

## 1. Goal

A small, standalone program that mines Yacoin proof-of-work blocks on the CPU
of the owner's laptop. It gets work from the local mainnet node
(`yacoind.service`) and hands solved blocks back to it.

It is a separate project. It does not change the yacoin repository. Code that
it needs is **copied** from yacoin, and each copied file records where it came
from (§6).

## 2. Scope

**In scope (first version):**

- Solo mining against one yacoind node over JSON-RPC, using the node's
  `getwork` interface.
- Multi-threaded CPU hashing with Yacoin's scrypt-jane (Keccak-512 + ChaCha20/8).
- Correctness tests that prove a hash found by the miner is the hash the node
  checks (§7).
- Start/stop, thread count, priority, hash-rate and status output.
- A short runbook for running it on the laptop.

**Out of scope (maybe later):**

- Pools and the stratum protocol.
- GPU mining (YACMiner and ccminer already exist for that).
- Building blocks in the miner itself (`getblocktemplate` + `submitblock`).
  `getwork` lets the node build the block, sign it and pay its own wallet,
  which keeps the miner simple.
- Proof-of-stake (minting). The node already does that from its wallet.
- Windows or macOS builds; non-x86-64 machines.

## 3. Facts from the node code

Checked against dev34253/yacoin `master` at 6859321 (2026-10-03), and
re-checked by the reviewer. These facts drive the design. A wrong byte here
means the miner hashes the wrong thing and can never find a valid block, so
each one gets a test (§7).

| # | Fact | Source |
|---|---|---|
| F1 | Current blocks are version 7, with a **64-bit `nTime`**. The hashed header is the packed 84-byte `struct block_header`: `version` (4), `prev_block` (32), `merkle_root` (32), `timestamp` (int64, 8), `bits` (4), `nonce` (4). The C++ `CBlock` object itself is *not* packed: on x86-64 there are 4 padding bytes at offsets 68–71 before the 8-byte-aligned time. The node copies around them (offsets 68 and 72). That is only valid on 64-bit ABIs. | `src/primitives/block.h:22-33`, `:126-145`; `src/miner.cpp:617-618`; `src/scrypt.cpp:210-211` |
| F2 | The PoW hash of a version-7 block is `scrypt_hash(header, 84, out, nFactorAtHardfork)`, chosen by **block version**, not height or time. `nFactorAtHardfork` is a node setting: `-nFactorAtHardfork`, default 21 (mainnet), 4 in yacoin's functional tests. `getmininginfo` reports it (`Nfactor`, `N`) once the height is past the fork. | `src/primitives/block.h:130-141`, `src/init.cpp:860`, `src/rpc/mining.cpp:313-316` |
| F3 | `scrypt_hash` calls scrypt-jane's `scrypt(input, len, salt = input, Nfactor, rfactor = 0, pfactor = 0, out, 32)`, built with `-DSCRYPT_KECCAK512 -DSCRYPT_CHACHA -DSCRYPT_CHOOSE_COMPILETIME` and `-O3 -msse2`. So N = 2^(Nfactor+1) = 2^22 at N-factor 21, and r = 1, p = 1. | `src/scrypt.cpp:108-128`, `src/scrypt-jane/scrypt-jane.c:152-203`, `src/Makefile.am:190-195` |
| F4 | That means **512 MiB of scratch memory per hash** (128 · r · N bytes) at N-factor 21, or 4 KiB at N-factor 4. scrypt-jane allocates and frees it on every call. Its self-test runs on the first call through a static flag that is not thread-safe, and calls `exit()` on failure. | `scrypt-jane.c:36-40`, `:181-203` |
| F5 | A block is valid PoW if `hash <= target`, compared as 256-bit little-endian numbers from the top word down. `target` comes from compact `nBits`. `CheckWork` also requires a PoW block (always true after the 2021 fork time) and that `prev_block` is still the tip. | `src/miner.cpp:640-655`, `src/pow.cpp:213-226`, `src/uint256.h:251-259`, `block.h:279` |
| F6 | `getwork` (no argument) returns `data` (128 bytes, hex), `target` (32 bytes, raw little-endian uint256, **not** word-reversed, so usable directly) and deprecated `midstate`/`hash1`. `data` is the 84-byte header, SHA-256-padded to 128 bytes (0x80 at byte 84, length 672 bits), with **every 32-bit word byte-reversed in place**. The 64-bit time is in words 17 and 18, each half reversed in place, not swapped. The nonce is word 20. | `src/rpc/mining.cpp:322-460`, `:426`; `src/miner.cpp:75-87`, `:603-635` (`:631-632` is the commented-out swap) |
| F7 | `getwork <data>` submits a solution. The node reverses the words back and uses `merkle_root` **only to find its saved block**. From the data it takes **only `timestamp` and `nonce`**; version, previous hash and nBits come from the saved block, and it rebuilds the merkle root from its saved coinbase. It works on a deep copy, **signs the block with the wallet** and runs `CheckWork`. | `src/rpc/mining.cpp:458-554`, `:516-539` |
| F8 | `getwork` refuses **both fetch and submit** while the node has **no connections** or is in initial block download: the check comes before the fetch/submit branch. A locked wallet makes the submit throw error -100 ("Unable to sign block, wallet locked?"). | `src/rpc/mining.cpp:339-343`, `:547-551` |
| F9 | The node makes a new block template when the tip changes, when there are new transactions and 60 s have passed, or after 0.75 · `nMaxClockDrift` = 5400 s. Each fetch also refreshes `nTime` and the extra nonce, which gives a new `merkle_root`. Saved blocks are kept until the tip changes. | `src/rpc/mining.cpp:364-400`, `src/primitives/block.cpp:54-57`, `src/main.h:54` |
| F10 | **The timestamp must be sent back unchanged.** Validation requires it to be later than the median time past, at most node time + 2 h, not earlier than any transaction time, and it must pass the coinbase time check. The miner never rolls the time; it fetches new work instead. | `src/validation.cpp:3191`, `:3205`, `:3249`, `:3253` |
| F11 | A submit returns plain `false` for four different reasons: "No saved block" (the tip changed and the saved blocks were cleared), PoW not met, stale (prev ≠ tip), or the node rejected the block. The reason appears only in `debug.log`. A submit takes a while, because the node computes one 512 MiB hash and runs `ProcessNewBlock`. | `src/rpc/mining.cpp:516-521`, `src/miner.cpp:648`, `:655`, `:681-685` |
| F12 | Every `getwork` fetch writes about 15 lines to `debug.log`, adds a saved block that stays until the tip changes, and reserves a wallet key. If the keypool is empty and the wallet locked, the payout script is null and the node dereferences it while building a block, which would probably crash it. | `src/rpc/mining.cpp:433-454`, `:385`; `src/wallet/wallet.cpp:4536-4540` |
| F13 | PoW blocks are signed by the node, but the signature is checked only for PoS blocks. The payout is pay-to-pubkey to the node's wallet. Coinbase maturity after the fork is 6 blocks. | `src/validation.cpp:3183`, `wallet.cpp:4540`, `src/consensus/consensus.h:35` |

## 4. Economics and expectations (measured 2026-10-03)

- Difficulty is at the minimum allowed: `nBits = 1e0fffff`, target
  `00000fffff00…`. On average a block takes 2^256 / target = 2^40 / 0xfffff
  ≈ **1,048,577 hashes**.
- **Difficulty will not rise.** It is recalculated only every 21,000 blocks,
  by height. The calculation clamps the elapsed time to at most 4× nominal,
  then takes the smaller of that result and 3× the hardest target since the
  fork, capped at the minimum difficulty (`src/pow.cpp:70-96`, `:141-155`,
  `chainparams.cpp:78`). At today's block rate the next recalculation is at
  height 1,974,000, years away, and it would still give `1e0fffff`.
- So the expected time per block depends only on **our own hash rate**:
  1,048,577 / H/s. Other miners don't make it slower. They only cost a
  little stale work when they find a block, and blocks are hours apart. At
  10 H/s that is about **29 hours per block**; at 5 H/s about 58 h; at
  20 H/s about 15 h.
- The node's estimate of the whole network is about 54 H/s
  (`getmininginfo` `netmhashps` = 5.4e-05). The last 12 blocks were 1.5 to
  20 hours apart.
- PoW reward: **5.17 YAC** per block (`powreward`). Rewards can be spent
  after 6 blocks (F13), so typically a day or more.
- CPU cost: during the sync, one hash at N-factor 21 took about 0.43 s while
  all 8 threads were busy (`debug.log`). A rough guess for this laptop is
  5–20 H/s in total; T-06 measures it.

The miner is a hobby and learning project, not an income source. Its value
is understanding Yacoin PoW end to end, plus a tool that the main yacoin
project could use for testing.

## 5. Design

- **Language and build:** C++17 with CMake and gcc, plus the copied C code of
  scrypt-jane. scrypt-jane's compile-time variant choice is gcc-only, so CMake
  checks the compiler. Dependencies: libcurl (HTTP/JSON-RPC) and a small
  header-only JSON library (for example nlohmann/json, vendored with its
  licence). No Boost or OpenSSL.
- **Modules:**
  - `hash/`: the copied scrypt-jane with the node's defines. One addition: an
    entry point that takes a caller-owned scratch buffer, so each thread
    allocates its buffer once instead of on every hash (F4). The scrypt-jane
    self-test runs once in `main` before any thread starts (F4).
  - `work/`: decode `getwork` `data` into the 84-byte header by undoing the
    word byte order (F6); set the nonce; encode the data for submission,
    keeping the node's timestamp unchanged (F10); parse the target.
  - `rpc/`: JSON-RPC client over HTTP with basic auth. Settings come from the
    command line, a config file, or a given `yacoin.conf` (`rpcuser`,
    `rpcpassword`, `rpcport`). Timeout of at least 30 s (F11).
  - `miner/`: N worker threads, each scanning its own slice of the 32-bit
    nonce range on the current work. A coordinator:
    - polls the cheap `getbestblockhash` every few seconds;
    - calls `getwork` only when the tip changes, after a submit, or every few
      minutes (well inside the 5400 s template age, F9), to keep
      `debug.log` noise and keypool use low (F12);
    - has workers drop stale work at once.
  - **Submitting:**
    - Before submitting, check the hash locally against the target.
    - If the submit is refused because the node has no peers or is in
      initial download (F8), keep the solution and retry until it is
      accepted or the tip changes (the saved block lives that long, F9).
    - "Stale" means the tip changed before the submit.
    - Log any other `false` with a pointer to the node's `debug.log` (F11).
  - **N-factor:** read `Nfactor` from `getmininginfo` at start-up and check it
    against `--nfactor` (default 21). The scratch size follows from it (F2,
    F4), so the same binary works on mainnet and on the N-factor-4 test chain.
  - `main`: options (`--threads`, `--nice`, `--rpc-*`, `--conf`, `--nfactor`,
    `--benchmark`), logging and periodic stats (H/s, found, accepted,
    rejected, stale, retried).
- **Memory:** 512 MiB per thread at N-factor 21. 8 GiB allows 16 threads in
  theory. On this 8-thread laptop, 4–6 threads (2–3 GiB) is the likely sweet
  spot, leaving room for the node, which also needs a temporary 512 MiB for
  each submit and block check. Optional transparent huge pages (`madvise`)
  for the scratch buffer; T-06 measures whether that helps.
- **Speed levers** (measured in T-06, not assumed):
  - The node already builds scrypt-jane with SSE2 (`-O3 -msse2`, F3).
    `-march=native` lets it pick the SSSE3 or AVX ChaCha code instead (there
    is no AVX2 ChaCha path).
  - Reusing the scratch buffer instead of allocating on every hash.
  - Thread count against memory bandwidth.

## 6. Copied code and licences

- `src/scrypt-jane/scrypt-jane.c` says "Public Domain or MIT License,
  whichever is easier" (`:1-5`). The `code/*.h` headers and `scrypt-jane.h`
  carry no notice of their own; they come from the same upstream project
  (floodyberry/scrypt-jane). yacoin's copy differs from upstream: `scrypt()`
  returns `int` and has a different signature.
- Copy `src/scrypt-jane/` into `third_party/scrypt-jane/` with a `SOURCE.md`
  naming the yacoin commit and paths, the upstream project, and the licence
  for the headers.
- `src/scrypt.cpp` is BSD-2-clause (`:1-28`). The `scrypt_hash` wrapper we
  need is a trivial call, so write our own instead of copying that file.
- yacoin itself is MIT (`COPYING`).
- The miner's own code: licence to be chosen by the owner (Q4).

## 7. Tests (the miner is only useful if these pass)

1. **Known-answer hash test:** take several real mainnet version-7 headers
   with `getblockheader <hash> false`, which returns the exact 84-byte
   serialized header: the snapshot tip 1,964,617, a block right after the
   1,890,000 fork, and one or two in between. Their scrypt hash with
   N-factor 21 must equal the block hash. This proves F1–F3. Generate the
   data once from the node (read-only calls) and commit it.
2. **getwork round trip:** `getwork` responses captured from the test chain
   (§7.4) decode to the right header fields, and re-encoding with a nonce
   gives exactly the bytes the node expects (F6, F7). Cross-check against the
   `raw_block_header_hex` that the node writes to `debug.log` for each call.
3. **Target test:** the `hash <= target` comparison and the
   `1e0fffff` → target conversion match the node (F5).
4. **Integration test on a private low-difficulty chain:**
   - Build yacoind with `--enable-low-difficulty-for-development` in a
     separate build directory (yacoin's `contrib/testing/build.sh --config
     lowdiff`).
   - Start two nodes linked only to each other, so that `getwork` sees a
     connection (F8). Use the same settings as yacoin's functional tests:
     `nFactorAtHardfork=4` and **`-testnetNewLogicBlockNumber=0`**.
     Without the second flag, new blocks are version 6 (80-byte header,
     date-based N-factor 21) and the miner rejects the work.
   - Check that the decoded work is version 7 and that `getmininginfo` shows
     `Nfactor` 4, `N` 32.
   - **Isolation:** the low-difficulty build keeps mainnet's network ID, magic
     bytes, port 7688 and fixed seeds (`chainparams.cpp:73-152`). So use
     isolated datadirs, `-connect` to each other only, `-dnsseed=0`,
     `-discover=0`, `-listen` and `-bind=127.0.0.1`, and non-default `-port`
     and `-rpcport`. It must never reach the mainnet node or real peers.
   - That build has no tip-age IBD check, so `getwork` works on a fresh chain
     once the two nodes are connected.
   - The miner must find blocks, the node must accept them, and
     `getblockcount` must rise. This harness is built early (right after
     T-01) so that T-03 can capture real `getwork` data from it.
5. **Submit retry test:** stop node 2, let the miner find a block (submit
   refused, no connections), restart node 2, and check that the retry is
   accepted.
6. **Benchmark mode:** hashes per second per thread count, without a node.

## 8. Running it on mainnet

- The node must have at least one peer (F8). With only one peer today, a
  block found while that peer is down is kept and retried until the tip
  changes (§5). More `addnode` peers are still recommended (owner's choice).
- The rewards go to the node's wallet. Wallet backup is the owner's job.
  Check that the keypool is not empty (F12).
- RPC credentials are in `/srv/yacoin/datadir/yacoin.conf`, readable only by
  the `yacoin` user (Q3).
- A runbook (T-07) covers start/stop, a `nice` level, an optional systemd
  unit, what to watch, and how to confirm that a found block was accepted and
  is in the main chain (and where in `debug.log` to look when a submit
  returns `false`).

## 9. Tasks

See `project/todo/`. Order:
1. T-01 (skeleton).
2. T-05a (low-difficulty test-chain harness).
3. T-02 (hash) and T-03 (getwork), which can run in parallel.
4. T-04 (mining loop).
5. T-05 (integration tests).
6. T-06 (performance).
7. T-07 (mainnet).

## 10. Risks

| Risk | Effect | Mitigation |
|---|---|---|
| Wrong header bytes or byte order | Miner never finds a valid block; silent waste | Known-answer tests from real blocks (§7.1, §7.2) before any mainnet run |
| Stale work after a new block or template | Wasted hashes, rejected submits | Poll `getbestblockhash`; drop work on a tip change; count stale submits |
| Node has no peer at submit time | Found block lost | Keep and retry until the tip changes (§5); test §7.5 |
| `false` from submit has four causes | Hard to diagnose | Local target check first; stale = tip changed; point to `debug.log` (F11) |
| 512 MiB per thread | Memory pressure, swapping | Cap threads by free memory; allocate once; warn at start-up |
| Locked wallet or empty keypool | Submit fails; empty keypool may crash the node (F12) | Check at start-up via RPC (`getinfo` `keypoolsize`); runbook |
| Polling side effects (log noise, keypool use) | Big `debug.log`, keys used up | `getwork` only on tip change or every few minutes (§5) |
| CPU contention with the node and test builds | Slower node, laptop heat | `nice`, thread limit, documented defaults |
| The node's `getwork` changes | Miner breaks | Pin the tested yacoin commit; integration test (§7.4) |

## 11. Review (2026-10-03)

An independent reviewer subagent checked this plan against the yacoin source
at 6859321. It found F1–F9 correct in substance. Its findings, all applied:

- the test chain needs `-testnetNewLogicBlockNumber=0` and network isolation;
- the economics were wrong (fixed difficulty means our rate alone decides);
- the difficulty recalculation is confirmed (§4), so T-07 no longer needs to check it;
- a submit while the node has no peers fails, so retry it;
- a submit can return `false` for four reasons;
- the timestamp must be sent back unchanged;
- the N-factor is a node setting;
- `getwork` polling has side effects;
- the SIMD baseline is SSE2, not generic;
- licence details;
- memory headroom;
- the scrypt-jane self-test is not thread-safe;
- `getblockheader … false` for test vectors;
- coinbase maturity is 6 blocks;
- several line references.
