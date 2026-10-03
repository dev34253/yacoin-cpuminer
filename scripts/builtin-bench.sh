#!/usr/bin/env bash
# T-06 step 1: hash rate of yacoind's own built-in miner at N-factor 21, for
# comparison with yacoin-cpuminer. Runs a THIRD, short-lived low-difficulty
# node (test-chain binaries) that is fully isolated: no P2P at all
# (-listen=0 -connect=0 -dnsseed=0), RPC on 127.0.0.1:27692 only, its own
# datadir under testchain/data/node3 (deleted first). Never the mainnet node.
#
#   scripts/builtin-bench.sh [THREADS] [SECONDS]      (default 1 thread, 150 s)
set -euo pipefail
cd "$(dirname "$0")/.."
THREADS=${1:-1}
SECS=${2:-150}
(( THREADS >= 1 && THREADS <= 8 )) || { echo "threads 1..8" >&2; exit 1; }
BIN=testchain/bin
D=testchain/data/node3
RPC=27692
P2P=27691
[[ "$("$BIN/yacoind" -version | head -1)" == *low-difficulty* ]] || { echo "not a low-difficulty build" >&2; exit 1; }
for p in $RPC $P2P; do [[ -z "$(ss -ltnH "sport = :$p")" ]] || { echo "port $p in use" >&2; exit 1; }; done
rm -rf "$D"; mkdir -p "$D"
printf 'rpcuser=bench\nrpcpassword=bench-not-secret\nrpcport=%s\nserver=1\n' $RPC >"$D/yacoin.conf"
chmod 600 "$D/yacoin.conf"
cli() { "$BIN/yacoin-cli" -datadir="$D" -conf="$D/yacoin.conf" -rpcconnect=127.0.0.1 -rpcport=$RPC "$@"; }

nice -n 10 "$BIN/yacoind" -datadir="$D" -conf="$D/yacoin.conf" -daemon=0 -server=1 -rpcbind=127.0.0.1 \
  -rpcallowip=127.0.0.1 -rpcport=$RPC -port=$P2P -listen=0 -connect=0 -dnsseed=0 -discover=0 -upnp=0 \
  -listenonion=0 -nFactorAtHardfork=21 -testnetNewLogicBlockNumber=0 -printtoconsole=0 >"$D/stdout.log" 2>&1 &
PID=$!
trap 'cli stop >/dev/null 2>&1 || kill $PID 2>/dev/null; wait $PID 2>/dev/null || true' EXIT
for i in $(seq 1 60); do cli getblockcount >/dev/null 2>&1 && break; sleep 1; done
echo "node3: $(cli getmininginfo | grep -E '"(Nfactor|N)"' | tr -d ' \n')"
cli setgenerate true "$THREADS" >/dev/null
sleep "$SECS"
cli setgenerate false >/dev/null || true
echo "hash meter lines (60 s windows, all threads):"
grep -E 'hashmeter|hash count' "$D/debug.log" | tail -6 || true
n=$(grep -c 'scrypt_hash(): Total time' "$D/debug.log" || true)
avg=$(grep -oE 'scrypt_hash\(\): Total time = [0-9]+' "$D/debug.log" | awk '{s += $NF; c++} END {if (c) printf "%.3f", s / c / 1e6}')
echo "scrypt_hash calls logged: $n, mean time per hash: ${avg:-?} s (threads $THREADS)"
