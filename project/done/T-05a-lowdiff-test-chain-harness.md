# T-05a: Low-difficulty test-chain harness

- Depends on: T-01
- Size: S
- Owner: Claude (subagent)
- Started: 2026-10-03
- Finished: 2026-10-03

## Goal
A script that starts a private two-node Yacoin chain at N-factor 4, so the
miner can be developed and tested without the mainnet node (plan §7.4).

## Steps
1. Build yacoind with `--enable-low-difficulty-for-development` in a separate build directory (yacoin `contrib/testing/build.sh --config lowdiff`, or reuse an existing lowdiff build). Record the yacoin commit.
2. Script `tests/testchain.sh start|stop|status`: two nodes with isolated datadirs, the settings yacoin's functional tests use (`nFactorAtHardfork=4`, `-testnetNewLogicBlockNumber=0`), `-connect` to each other only, `-dnsseed=0`, `-discover=0`, `-listen -bind=127.0.0.1`, non-default `-port`/`-rpcport`, fresh wallets.
3. Isolation check: the low-difficulty build keeps mainnet's network ID, magic, port 7688 and fixed seeds, so the script refuses to start if the ports or flags would let it reach real peers or the mainnet node.
4. Check: `getmininginfo` shows `Nfactor` 4 and `N` 32; one `getwork` returns version-7 data; capture a few `getwork` responses and the matching `raw_block_header_hex` lines for T-03.

## Acceptance criteria
- [x] `testchain.sh start` brings up two connected nodes; `getwork` works; captured samples committed.
- [x] Never touches `/srv/yacoin` or the mainnet node.

## Log
- Binaries: not rebuilt; copied the existing low-difficulty build from
  `~/.cache/yacoin-build-P0-50/build-lowdiff/src` into `testchain/bin/`
  (git-ignored) with `tests/testchain.sh install`. Built from yacoin
  `85e11f325da0` (merge of master into P0-08, equivalent to master 7ed6a83a);
  the binary reports `YAC-v1.11.0.0-2f098ffde962-dirty-leveldb-low-difficulty`.
  SHA256: yacoind `6061297e1ff8085d976df56951cd68b71796a45dcd73d35a68bd72de6ad51bba`,
  yacoin-cli `dc5c71201e462ad8ff35f96a2aa22b52821f1e82630006f4884f8571f0081d81`.
- `tests/testchain.sh` uses the functional-test settings (`util.py`
  `initialize_datadir`, `test_node.py`): `nFactorAtHardfork=4`,
  `-testnetNewLogicBlockNumber=0`, `epochinterval=10`, `checkblocks=8`,
  `dnsseed=0`, `upnp=0`, `logtimestamps`, `logtimemicros`; plus isolation:
  `-connect` to the other node only (checked in `net.cpp`
  `ThreadOpenConnections`: with `-connect` the node never uses DNS or fixed
  seeds), `-listen=1 -bind=127.0.0.1 -discover=0 -listenonion=0`,
  `-rpcbind=127.0.0.1`, ports 27688/27689 (P2P) and 27687/27690 (RPC).
  Refuses to start if a port is in use or the binary is not low-difficulty;
  the RPC helper refuses any port other than 27687/27690.
- Run: both nodes up, `getpeerinfo` shows only each other (127.0.0.1:27688 ↔
  27689); `getmininginfo` `Nfactor` 4, `N` 32; `getwork` returns version-7
  data (first word `00000007`), nBits `2000ffff` (initialHashTarget ~0>>8).
- `tests/testchain.sh capture 3` → `tests/data/getwork_samples.json`
  (data, target and the node's logged `raw_block_header_hex` and fields).
- Note: the low-difficulty powLimit is ~0>>3 and `epochinterval=10` retargets
  every 10 blocks, so the test chain gets harder as blocks are found.
