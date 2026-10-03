# T-03: getwork RPC client and data encoding

- Depends on: T-01, T-05a
- Size: S
- Owner: Claude (subagent)
- Started: 2026-10-03
- Finished: 2026-10-03

## Goal
Talk to yacoind: fetch work, decode it, encode and submit solutions (plan F6–F9).

## Steps
1. JSON-RPC over HTTP with basic auth (libcurl), timeout at least 30 s (a submit computes a 512 MiB hash on the node); settings from options, a config file or a `yacoin.conf` (Q3).
2. Decode `data`: undo the per-word byte reversal (64-bit time in words 17–18, each half reversed in place; nonce in word 20), take the first 84 bytes as the header; `target` is raw little-endian, not word-reversed.
3. Encode a solution: the node's header with our nonce and the node's timestamp **unchanged** (plan F10) back into the 128-byte word-reversed form. Only timestamp and nonce are used by the node; merkle_root only finds the saved block (F7).
4. Also: `getbestblockhash` (cheap tip poll), `getmininginfo` (N-factor), `getinfo` (keypool size, wallet state).
5. Error handling: no connections / initial download (applies to fetch **and** submit, F8) → keep and retry the solution; auth failure; error -100 locked wallet; plain `false` (four possible reasons, F11).
6. Tests with `getwork` responses captured from the low-difficulty test chain (T-05a): decode → fields; encode → exact bytes; check against the `raw_block_header_hex` the node logs for the same call.

## Acceptance criteria
- [x] Round-trip tests pass.
- [x] One live `getwork` fetch against the mainnet node decodes to a sane header (prev hash = node tip, version 7, nBits 1e0fffff). Note: a fetch is **not** read-only; it creates a saved block, reserves a wallet key and logs about 15 lines (F12). One call is fine.

## Log
- libcurl JSON-RPC client (`src/rpc/rpc_client.cpp`): basic auth in-process
  (CURLOPT_USERNAME/PASSWORD, cleared after each call), proxy disabled,
  timeout 120 s; 401/403 → auth error, JSON error object → `RpcError(code)`,
  other failures → transport error. `classify_rpc_failure`: -9, -10, -28,
  transport → transient (retry); -100 → wallet locked; 401/403 → fatal.
- Settings: `~/.config/yacoin-cpuminer/miner.conf` by default, `--conf`,
  `--yacoin-conf` (parsed like the node: '#' anywhere, first key wins,
  sections ignored), `--rpc-host/--rpc-port/--rpc-user`. `--rpc-password` is
  refused on purpose (it would show in `ps`). Warning if a config file is
  group/world accessible.
- `decode_getwork` / `encode_getwork_submit` (`src/work/getwork.cpp`): undo
  the per-word reversal, check padding (0x80 at 84, 0x02a0 at 126..127) and
  version ≥ 7; encode changes only the nonce (word 20), time and merkle root
  go back byte-exact.
- Tests (`tests/test_getwork.cpp`, `test_rpc.cpp`, `test_target.cpp`): the
  three captured test-chain samples decode to exactly the node's logged
  `raw_block_header_hex` and fields; encode(nonce 0) == the node's data;
  another nonce changes only word 20 and the node-side parse reads back the
  nonce and the unchanged time; time ≥ 2^32 round trip; fake HTTP server for
  auth header, error classes, 404/503/200-with-error/403, node down.
- `yacoin-cpuminer --check-work` (one getwork, decode, checks, never submits).
- **Live mainnet check (the single allowed getwork, 2026-10-03 12:42:59):**
  version 7, prev `00000384e8e1…17bf` == node tip (height 1,964,617),
  bits `1e0fffff`, target `00000fffff00…` (~1,048,577 hashes/block),
  node Nfactor 21. All checks ok. No further getwork calls on mainnet.
- Mainnet `getinfo` at that time: 1 connection, keypoolsize 200, no
  `unlocked_until` (wallet not encrypted, so signing works).
- **Review** (reviewer subagent over the staged T-02/T-03 diff): no
  correctness bugs in hash, header layout, getwork encode/decode, target.
  Applied: yacoin.conf parsed like the node (finding 1); self-test sets the
  scrypt-jane flag (2); 403 has its own message (3); type checks on error
  objects and replies (4); config numbers checked with the key name (5);
  permission warning wired into main (6); comment on compact size 0 (7);
  test gaps: node-computed N-factor-4 vectors, time ≥ 2^32 test, HTTP edge
  cases, clarified the huge-pages and node-format test comments. Not applied:
  nothing.
