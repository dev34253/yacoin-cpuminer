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

## Private test chain

`tests/testchain.sh` runs two low-difficulty yacoind nodes (N-factor 4,
version-7 blocks from height 0) linked only to each other on loopback ports
27688/27689 (P2P) and 27687/27690 (RPC). It needs a yacoind built with
`--enable-low-difficulty-for-development` (yacoin `contrib/testing/build.sh
--config lowdiff`):

```sh
tests/testchain.sh install ~/path/to/build-lowdiff/src   # copies into testchain/bin (git-ignored)
tests/testchain.sh start      # fresh datadirs in testchain/data, waits until connected
tests/testchain.sh status
tests/testchain.sh cli 1 getmininginfo
tests/testchain.sh stop
```

The low-difficulty build keeps mainnet's magic bytes and port, so the script
isolates the nodes (`-connect` to each other only, `-bind=127.0.0.1`,
`-dnsseed=0`, `-discover=0`) and refuses to start if a port is in use or the
binary is not a low-difficulty build.

## Licence

MIT (`LICENSE`). Copied code in `third_party/` keeps its own licence; see the
`SOURCE.md` files there.
