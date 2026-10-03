# Provenance: scrypt-jane

- **Copied from:** the yacoin node, https://github.com/dev34253/yacoin,
  `src/scrypt-jane/` (all files: `scrypt-jane.c`, `scrypt-jane.h`, `code/*.h`).
- **yacoin commit:** `5183b3ae2ae8271b864cf912810e8f21f01a6e73` (`origin/master`,
  2026-10-03). The directory was last changed in yacoin commit
  `48637d7b918043b6369c0a7aa19791d3cf7f4cb2` (2025-08-17).
- **Copied on:** 2026-10-03, byte for byte (`git show origin/master:<path>`).
  No file in this directory is modified. To check:
  `for f in $(git -C <yacoin> ls-tree -r --name-only 5183b3ae src/scrypt-jane); do git -C <yacoin> show 5183b3ae:$f | cmp - third_party/scrypt-jane/${f#src/scrypt-jane/}; done`
- **Upstream:** scrypt-jane by Andrew Moon, https://github.com/floodyberry/scrypt-jane.
  yacoin's copy differs from upstream: `scrypt()` returns `int` (1 = success) and
  takes separate `rfactor`/`pfactor` arguments, plus small portability edits.

## Licence

- `scrypt-jane.c` states: "Public Domain or MIT License, whichever is easier".
- `scrypt-jane.h` and `code/*.h` carry no notice of their own. They are part of
  the same upstream project, whose README states the same terms (public domain
  or MIT). We use them under the MIT licence.
- yacoin itself is MIT licensed (`COPYING`).

## How it is built here

`src/hash/yac_scrypt.c` `#include`s `scrypt-jane.c` and adds our own entry
points (self-test, caller-owned scratch buffer). It is compiled with the node's
defines `-DSCRYPT_KECCAK512 -DSCRYPT_CHACHA -DSCRYPT_CHOOSE_COMPILETIME` and
`-O3`, plus `-msse2` (the node's baseline) or `-march=native` (CMake option
`YAC_NATIVE`, default ON). The ChaCha variant is chosen at compile time from the
`-m` flags: SSE2 with `-msse2`, AVX with `-march=native` on an AVX CPU.

yacoin's `src/scrypt.cpp` (BSD-2-clause) is **not** copied; its `scrypt_hash`
wrapper is a one-line call, re-written in `src/hash/pow_hash.cpp`.
