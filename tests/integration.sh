#!/usr/bin/env bash
# End-to-end test on the private low-difficulty chain (plan §7.4, §7.5; task T-05).
#
#   tests/integration.sh [--keep-running]
#
# Needs: build/yacoin-cpuminer (scripts/build.sh) and the test-chain binaries
# (tests/testchain.sh install). Starts a FRESH two-node test chain, then:
#   A. accept   – two separate miner runs, 5 blocks each; every accepted hash is
#                 in both nodes' chains and in node 1's debug.log CheckWork lines.
#   ramp        – mine until a block takes tens of seconds for one thread.
#   B. stale    – while the miner works, node 2 mines a block itself
#                 (generatetoaddress); the miner must drop its work and switch
#                 to work on node 2's block.
#   C. retry    – stop node 2 (node 1 then has no peers, getwork refuses submits,
#                 plan F8); the miner finds a block, the submit is refused and
#                 kept; restart node 2; the retried submit is accepted.
# Never touches the mainnet node: the miner gets node 1's config explicitly.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TC="$ROOT/tests/testchain.sh"
MINER="$ROOT/build/yacoin-cpuminer"
OUT="$ROOT/testchain/integration"
KEEP=0
[[ "${1:-}" == "--keep-running" ]] && KEEP=1

[[ -x "$MINER" ]] || { echo "build the miner first: scripts/build.sh" >&2; exit 1; }
mkdir -p "$OUT"
rm -f "$OUT"/*.log
CONF1="$("$TC" conf 1)"
MINER_ARGS=(--conf "$CONF1" --nfactor 4 --nice 10 --tip-poll 1 --retry 2 --stats-interval 20)

FAILS=0
MINER_PID=""
pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; FAILS=$((FAILS + 1)); }
cli() { "$TC" cli "$@"; }

cleanup() {
  if [[ -n "$MINER_PID" ]] && kill -0 "$MINER_PID" 2>/dev/null; then kill -INT "$MINER_PID"; wait "$MINER_PID" || true; fi
  if [[ $KEEP == 0 ]]; then "$TC" stop >/dev/null || true; fi
}
trap cleanup EXIT

wait_for() {  # wait_for SECONDS COMMAND...  (true when COMMAND succeeds in time)
  local t=$1; shift
  local end=$((SECONDS + t))
  while ((SECONDS < end)); do "$@" && return 0; sleep 1; done
  "$@"
}

heights_equal() { [[ "$(cli 1 getblockcount)" == "$(cli 2 getblockcount)" ]]; }

check_accepted_blocks() {  # check_accepted_blocks LOGFILE EXPECTED_COUNT
  local log=$1 want=$2 n=0 h
  wait_for 30 heights_equal || fail "nodes did not reach the same height"
  for h in $(grep -oE 'ACCEPTED block [0-9a-f]{64}' "$log" | awk '{print $3}'); do
    n=$((n + 1))
    local c1 c2
    c1=$(cli 1 getblock "$h" | grep -oE '"confirmations" *: *-?[0-9]+' | grep -oE -- '-?[0-9]+$' || echo missing)
    c2=$(cli 2 getblock "$h" | grep -oE '"confirmations" *: *-?[0-9]+' | grep -oE -- '-?[0-9]+$' || echo missing)
    if [[ "$c1" =~ ^[0-9]+$ && "$c1" -ge 1 && "$c2" =~ ^[0-9]+$ && "$c2" -ge 1 ]]; then :; else
      fail "block $h not in the main chain of both nodes (confirmations: $c1 / $c2)"
    fi
    grep -q "hash: $h" "$ROOT/testchain/data/node1/debug.log" || fail "block $h has no CheckWork line in node 1's debug.log"
  done
  [[ $n -ge $want ]] && pass "$n accepted blocks in $log are in both chains and in node 1's CheckWork log" ||
    fail "only $n accepted blocks in $log (want $want)"
}

start_miner() {  # start_miner LOGFILE extra args...
  local log=$1; shift
  "$MINER" "${MINER_ARGS[@]}" "$@" >"$log" 2>&1 &
  MINER_PID=$!
}

stop_miner() {
  kill -INT "$MINER_PID"
  local rc=0
  wait "$MINER_PID" || rc=$?
  MINER_PID=""
  return $rc
}

echo "== fresh test chain"
"$TC" stop >/dev/null
"$TC" start

echo "== A. accept: 2 runs x 5 blocks"
for run in 1 2; do
  h0=$(cli 1 getblockcount)
  "$MINER" "${MINER_ARGS[@]}" --threads 2 --max-blocks 5 >"$OUT/accept$run.log" 2>&1 || fail "miner run $run exited non-zero"
  wait_for 30 heights_equal || true
  h1=$(cli 2 getblockcount)
  [[ $((h1 - h0)) -ge 5 ]] && pass "run $run: height $h0 -> $h1 on node 2" || fail "run $run: height $h0 -> $h1"
  check_accepted_blocks "$OUT/accept$run.log" 5
  grep -q 'rejected 0,' "$OUT/accept$run.log" && pass "run $run: no rejected submits" || fail "run $run: rejected submits"
done

echo "== ramp difficulty (target: >= ~20 s per block for one thread)"
"$MINER" "${MINER_ARGS[@]}" --threads 2 --max-blocks 70 >"$OUT/ramp.log" 2>&1 || fail "ramp run failed"
wait_for 30 heights_equal || true
echo "   height $(cli 1 getblockcount), bits $(cli 1 getblock "$(cli 1 getbestblockhash)" | grep -oE '"bits" *: *"[0-9a-f]+"')"

echo "== B. stale work: node 2 mines a block while the miner works"
start_miner "$OUT/stale.log" --threads 1 --work-refresh 3600
wait_for 30 grep -q 'new work' "$OUT/stale.log" || fail "miner got no work"
ADDR2=$(cli 2 getnewaddress)
H2=$(cli 2 generatetoaddress 1 "$ADDR2" 2000000000 | grep -oE '[0-9a-f]{64}' | head -1)
echo "   node 2 mined $H2"
if wait_for 30 grep -q "parent $H2" "$OUT/stale.log"; then
  pass "miner dropped its work and mines on node 2's block $H2"
else
  # The miner may have built on top of node 2's block already (its own block came first).
  fail "miner never got work on node 2's block $H2"
fi
grep -E 'tip changed|STALE' "$OUT/stale.log" | sed 's/^/   /' | head -5 || true

echo "== C. submit retry while node 1 has no peers"
"$TC" stop-node 2
wait_for 30 bash -c "[[ \$('$TC' cli 1 getconnectioncount) == 0 ]]" && pass "node 1 has no peers" || fail "node 1 still has peers"
echo "   waiting for the miner to find a block (up to 15 min)..."
if wait_for 900 grep -q 'submit refused' "$OUT/stale.log"; then
  pass "submit refused while node 1 has no peers; solution kept"
  grep -m1 'submit refused' "$OUT/stale.log" | sed 's/^/   /'
  sleep 5
  "$TC" start-node 2
  if wait_for 120 bash -c "grep -A100 'submit refused' '$OUT/stale.log' | grep -q ACCEPTED"; then
    pass "retried submit accepted after node 2 came back"
    RETRY_HASH=$(grep -A100 'submit refused' "$OUT/stale.log" | grep -m1 -oE 'ACCEPTED block [0-9a-f]{64}' | awk '{print $3}')
    echo "   $RETRY_HASH"
  else
    fail "retried submit was not accepted"
  fi
else
  fail "no refused submit within 15 min"
  "$TC" start-node 2
fi

stop_miner && pass "miner stopped cleanly on SIGINT" || fail "miner exit code non-zero"
grep 'final:' "$OUT/stale.log" | sed 's/^/   /'
grep -qE 'retried [1-9]' "$OUT/stale.log" && pass "retry counter > 0" || fail "retry counter is 0"
check_accepted_blocks "$OUT/stale.log" 1

echo
echo "height: node 1 $(cli 1 getblockcount), node 2 $(cli 2 getblockcount); logs in $OUT"
if [[ $FAILS == 0 ]]; then echo "INTEGRATION TEST PASSED"; else echo "INTEGRATION TEST FAILED ($FAILS)"; exit 1; fi
