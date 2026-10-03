# T-02: Hash core and known-answer tests

- Depends on: T-01
- Size: S
- Owner:
- Started:
- Finished:

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
- [ ] All known-answer tests pass with and without `-march=native`.
- [ ] Hashes per second per thread are printed by a micro-benchmark (input for T-06).

## Log
-
