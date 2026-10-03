# Working rules for this project (yacoin-cpuminer)

A standalone CPU miner for Yacoin. Plan: `project/plans/plan.md`; owner
decisions: `project/open-questions.md`.

## Rules

1. **Tests before every commit.** `scripts/build.sh` (configure, build, `ctest`)
   must pass. Run it with `-DYAC_NATIVE=OFF` too when hashing code changes.
2. **Review before every commit.** At least a deliberate self-review of the
   diff; for hashing/encoding, the mining loop and docs, a reviewer subagent
   with concrete file:line findings. Log what was not applied and why in the
   task file.
3. **Docs updated with the code.** README, runbooks and task files change in
   the same commit as the behaviour they describe.
4. **Task board.** Tasks live in `project/{todo,inprogress,done}/`. Move them
   with `git mv`; fill in Owner, Started, Finished; keep a short Log with the
   results (numbers, commands, commit SHAs).
5. Commit messages end with the `Co-Authored-By:` line the session asks for.
   Local repository only (branch `main`, no remote) until the owner decides.

## Safety

- **Mainnet node** (`yacoind.service`, RPC 127.0.0.1:7687): only read-only RPCs
  (`getblockcount`, `getblockhash`, `getblockheader`, `getblock`,
  `getbestblockhash`, `getmininginfo`, `getinfo`) unless the owner runs the
  miner. Never touch `/srv/yacoin`, the service or its config.
- **Credentials** live in `~/.config/yacoin-cpuminer/miner.conf` (mode 600).
  Never print, log or commit the password. With the curl CLI, pass it via
  `--config -` on stdin, never on the command line.
- **Test chain** (`tests/testchain.sh`): the low-difficulty yacoind keeps
  mainnet's magic and ports, so it runs only with isolated datadirs,
  `-connect` between its two nodes, `-bind=127.0.0.1` and the ports
  27688/27689 (P2P), 27687/27690 (RPC).
- **CPU/memory:** builds with `nice -n 10`, at most `-j4`. One hash at
  N-factor 21 needs 512 MiB; never benchmark with more than 8 threads.

## Code facts that matter

- The hashed header is the packed 84-byte version-7 header (64-bit time);
  PoW = scrypt-jane(Keccak-512, ChaCha20/8, N = 2^(Nfactor+1), r = p = 1),
  salt = input (plan F1–F3). Known-answer tests in `tests/test_hash.cpp`
  prove it against real mainnet blocks.
- `getwork` data is the header SHA-256-padded to 128 bytes with every 32-bit
  word byte-reversed; the node reads back only time and nonce (plan F6, F7).
- `third_party/` is copied code; do not edit it, see each `SOURCE.md`.
