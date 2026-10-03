# yacoin-cpuminer

A small, standalone CPU miner for Yacoin proof-of-work blocks. It gets work
from a local `yacoind` over JSON-RPC (`getwork`) and hands solved blocks back.

Status: under construction (see `project/`).

## Build

Requirements: Linux x86-64, gcc/g++ (C++17), CMake ≥ 3.16, libcurl development
headers.

```sh
sudo apt install build-essential cmake libcurl4-openssl-dev
scripts/build.sh            # configure + build + ctest, in build/
```

`scripts/build.sh` falls back to `uvx --from cmake` and a locally unpacked
`libcurl4-openssl-dev` (no sudo) when cmake or the curl headers are missing.

CMake option `-DYAC_NATIVE=OFF` builds with the node's baseline `-msse2`
instead of `-march=native`.

## Licence

MIT (`LICENSE`). Copied code in `third_party/` keeps its own licence; see the
`SOURCE.md` files there.
