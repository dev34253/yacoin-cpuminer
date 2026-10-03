# T-01: Repository skeleton and build

- Depends on: –
- Size: S
- Owner:
- Started:
- Finished:

## Goal
A buildable empty miner with the copied hash code in place.

## Steps
1. CMake project (C++17), `src/`, `tests/`, `third_party/`, `CLAUDE.md` with the working rules, a README and a LICENSE (Q4).
2. Copy yacoin `src/scrypt-jane/` to `third_party/scrypt-jane/` with `SOURCE.md` (yacoin commit and paths, upstream floodyberry/scrypt-jane, licence of the unlabelled `code/` headers) (plan §6). Do not copy `src/scrypt.cpp` (BSD-2); write the small `scrypt_hash` wrapper ourselves.
3. Vendor the JSON library; find libcurl with CMake.
4. Build with gcc (scrypt-jane compile-time selection is gcc-only; CMake checks it) and the node's defines `-DSCRYPT_KECCAK512 -DSCRYPT_CHACHA -DSCRYPT_CHOOSE_COMPILETIME`, `-O3 -msse2` as baseline, and an option for `-march=native` (default on for local builds).
5. A test runner (for example a minimal test framework or Catch2 single header) and one trivial test.

## Acceptance criteria
- [ ] `cmake -B build && cmake --build build && ctest --test-dir build` passes on the laptop.
- [ ] Provenance and licence notices for every copied file.

## Log
-
