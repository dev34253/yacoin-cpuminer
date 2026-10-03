# T-03: getwork RPC client and data encoding

- Depends on: T-01, T-05a
- Size: S
- Owner:
- Started:
- Finished:

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
- [ ] Round-trip tests pass.
- [ ] One live `getwork` fetch against the mainnet node decodes to a sane header (prev hash = node tip, version 7, nBits 1e0fffff). Note: a fetch is **not** read-only; it creates a saved block, reserves a wallet key and logs about 15 lines (F12). One call is fine.

## Log
-
