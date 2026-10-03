# T-01: Repository skeleton and build

- Depends on: –
- Size: S
- Owner: Claude (subagent)
- Started: 2026-10-03
- Finished: 2026-10-03

## Goal
A buildable empty miner with the copied hash code in place.

## Steps
1. CMake project (C++17), `src/`, `tests/`, `third_party/`, `CLAUDE.md` with the working rules, a README and a LICENSE (Q4).
2. Copy yacoin `src/scrypt-jane/` to `third_party/scrypt-jane/` with `SOURCE.md` (yacoin commit and paths, upstream floodyberry/scrypt-jane, licence of the unlabelled `code/` headers) (plan §6). Do not copy `src/scrypt.cpp` (BSD-2); write the small `scrypt_hash` wrapper ourselves.
3. Vendor the JSON library; find libcurl with CMake.
4. Build with gcc (scrypt-jane compile-time selection is gcc-only; CMake checks it) and the node's defines `-DSCRYPT_KECCAK512 -DSCRYPT_CHACHA -DSCRYPT_CHOOSE_COMPILETIME`, `-O3 -msse2` as baseline, and an option for `-march=native` (default on for local builds).
5. A test runner (for example a minimal test framework or Catch2 single header) and one trivial test.

## Acceptance criteria
- [x] `cmake -B build && cmake --build build && ctest --test-dir build` passes on the laptop.
- [x] Provenance and licence notices for every copied file.

## Log
- Environment: gcc 15.2.0. **cmake and libcurl4-openssl-dev are not installed**
  (no sudo used). Workaround in `scripts/build.sh`: cmake 4.4.3 from PyPI via
  `uvx --from cmake`; `libcurl4-openssl-dev` 8.18.0-1ubuntu2.7 fetched with
  `apt-get download` and unpacked with `dpkg -x` into
  `~/.cache/yacoin-cpuminer-deps`, linked to the system `libcurl.so.4` (same
  version). Proper fix for the owner: `sudo apt install cmake libcurl4-openssl-dev`.
- scrypt-jane copied byte for byte from yacoin `5183b3ae` (`origin/master`);
  `cmp` check in `third_party/scrypt-jane/SOURCE.md` passes. Upstream README
  confirms "Public Domain, or MIT" for the unlabelled headers.
- `src/hash/yac_scrypt.c` `#include`s the unmodified `scrypt-jane.c` (one
  copy of the mix code) and adds our entry points.
- Checked: SIMD variant is chosen by the `-m` flags: `-O3 -msse2` → ChaCha/8-SSE2,
  `-march=native` → ChaCha/8-AVX on this i5-8300H; same scrypt output.
- nlohmann/json 3.12.0 vendored (SHA256 in its SOURCE.md). Own minimal test
  framework `tests/testing.h` (one ctest per test binary).
- `scripts/build.sh` (native) and `--build-dir build-sse2 -DYAC_NATIVE=OFF`:
  build OK, ctest 1/1 passed (hex, scrypt-jane self-test).
