# T-05a: Low-difficulty test-chain harness

- Depends on: T-01
- Size: S
- Owner:
- Started:
- Finished:

## Goal
A script that starts a private two-node Yacoin chain at N-factor 4, so the
miner can be developed and tested without the mainnet node (plan §7.4).

## Steps
1. Build yacoind with `--enable-low-difficulty-for-development` in a separate build directory (yacoin `contrib/testing/build.sh --config lowdiff`, or reuse an existing lowdiff build). Record the yacoin commit.
2. Script `tests/testchain.sh start|stop|status`: two nodes with isolated datadirs, the settings yacoin's functional tests use (`nFactorAtHardfork=4`, `-testnetNewLogicBlockNumber=0`), `-connect` to each other only, `-dnsseed=0`, `-discover=0`, `-listen -bind=127.0.0.1`, non-default `-port`/`-rpcport`, fresh wallets.
3. Isolation check: the low-difficulty build keeps mainnet's network ID, magic, port 7688 and fixed seeds, so the script refuses to start if the ports or flags would let it reach real peers or the mainnet node.
4. Check: `getmininginfo` shows `Nfactor` 4 and `N` 32; one `getwork` returns version-7 data; capture a few `getwork` responses and the matching `raw_block_header_hex` lines for T-03.

## Acceptance criteria
- [ ] `testchain.sh start` brings up two connected nodes; `getwork` works; captured samples committed.
- [ ] Never touches `/srv/yacoin` or the mainnet node.

## Log
-
