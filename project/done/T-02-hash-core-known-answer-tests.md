# T-02: Hash core and known-answer tests

- Depends on: T-01
- Size: S
- Owner: Claude (subagent)
- Started: 2026-10-03
- Finished: 2026-10-03

## Goal
Prove that the miner computes exactly the node's PoW hash (plan F1–F4).

## Steps
1. Hash entry point: 84-byte header → `scrypt` (Keccak-512, ChaCha, N-factor given, r = p = 1), as yacoin `scrypt_hash`.
2. Variant that takes a caller-owned 512 MiB scratch buffer (allocated once per thread); check that it gives identical output to the allocating version.
3. Generate test vectors from the mainnet node with read-only RPC: `getblockheader <hash> false` (exact 84-byte serialized header) and the block hash, for a few version-7 blocks: the 1,964,617 tip, one just after 1,890,000, two in between. Commit them as a small file.
4. Test: header built from the fields → hash with N-factor 21 == block hash, for every vector.
5. Reject work with version < 7 (80-byte header, date-based N-factor) with a clear error.
6. Run the scrypt-jane self-test once at start-up, before any worker thread (it is not thread-safe and calls `exit()` on failure).
7. Scratch size from the N-factor (512 MiB at 21, 4 KiB at 4).

## Acceptance criteria
- [x] All known-answer tests pass with and without `-march=native`.
- [x] Hashes per second per thread are printed by a micro-benchmark (input for T-06).

## Log
- `src/hash/yac_scrypt.c`: `yac_scrypt_hash_scratch` repeats scrypt-jane's
  `scrypt()` steps (PBKDF2 → ROMix → PBKDF2, salt = input, r = p = 1, 32-byte
  output) on a caller-owned, 64-byte aligned buffer of
  (2^(Nf+1)+2)·128 bytes (512 MiB + 256 B at 21, 4 KiB + 256 B at 4).
  `ScryptHasher` (pow_hash.cpp) mmaps it once per thread, optionally 2 MiB
  aligned with `madvise(MADV_HUGEPAGE)`.
- Test vectors from mainnet with read-only RPC (`scripts/rpc-readonly.sh`,
  password via curl `--config -`): heights 1,890,000 (fork block), 1,890,001,
  1,910,000, 1,940,000, 1,964,617 (tip), all version 7, proof-of-work →
  `tests/data/mainnet_headers.json`. Plus three blocks at N-factor 4 mined by
  the low-difficulty yacoind itself (`generatetoaddress` on the test chain) →
  `tests/data/testchain_headers.json`.
- `tests/test_hash.cpp`: header rebuilt from the fields == `getblockheader
  … false` bytes; scrypt(N-factor 21) == block hash for all 5 mainnet blocks;
  N-factor 4 hash == node's hash for 3 test-chain blocks; scratch == allocating
  reference (also with huge pages requested; 0,1,4,9 × 50 inputs).
  **Passes with `-march=native` (ChaCha/8-AVX) and `-DYAC_NATIVE=OFF`
  (`-msse2`, ChaCha/8-SSE2).**
- Micro-benchmark (1 thread, N-factor 21, reused buffer): 0.84 H/s AVX,
  0.77 H/s SSE2 (≈1.1–1.2 s per hash; first hash ≈1.4 s with page faults;
  the allocating reference ≈1.45 s).
- Version < 7 → `UnsupportedWork` with a pointer to `-testnetNewLogicBlockNumber=0`
  (in `decode_getwork`, test `rejects_old_versions_and_bad_data`).
- Self-test: `scrypt_self_test()` makes the first `scrypt()` call from main,
  which runs the power-on self-test and sets scrypt-jane's static flag
  (review finding 2).
- Review: see T-03 log (one reviewer pass over T-02 + T-03).
