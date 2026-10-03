#!/usr/bin/env bash
# T-06 benchmark matrix: H/s at an N-factor for several thread counts, under
# nice 10, with the package temperature, the mainnet node's CPU use and the
# miner's huge-page use sampled during each run. No node RPC is used.
#
#   scripts/bench.sh [--bin build/yacoin-cpuminer] [--seconds 90] [--nfactor 21]
#                    [--no-hugepages] [--label TEXT] THREADS...
#
# Prints one table row per run (markdown). Never more than 8 threads.
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=build/yacoin-cpuminer
SECS=90
NF=21
HP=--hugepages
LABEL=""
THREADS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --bin) BIN=$2; shift 2 ;;
    --seconds) SECS=$2; shift 2 ;;
    --nfactor) NF=$2; shift 2 ;;
    --no-hugepages) HP=--no-hugepages; shift ;;
    --label) LABEL=$2; shift 2 ;;
    *) THREADS+=("$1"); shift ;;
  esac
done

pkg_zone=""
for z in /sys/class/thermal/thermal_zone*; do
  [[ "$(cat "$z/type" 2>/dev/null)" == x86_pkg_temp ]] && pkg_zone="$z/temp"
done

node_cpu_ticks() {  # utime+stime of all yacoind processes (clock ticks)
  local t=0 p
  for p in $(pgrep -x yacoind || true); do
    t=$((t + $(awk '{print $14 + $15}' "/proc/$p/stat" 2>/dev/null || echo 0)))
  done
  echo $t
}

hz=$(getconf CLK_TCK)
for n in "${THREADS[@]}"; do
  (( n >= 1 && n <= 8 )) || { echo "threads must be 1..8" >&2; exit 1; }
  log=$(mktemp)
  c0=$(node_cpu_ticks); t0=$(date +%s.%N)
  nice -n 10 "$BIN" --benchmark --threads "$n" --nfactor "$NF" --bench-seconds "$SECS" "$HP" --nice 0 >"$log" 2>&1 &
  pid=$!
  maxtemp=0; hugekb=0
  while kill -0 $pid 2>/dev/null; do
    if [[ -n $pkg_zone ]]; then t=$(( $(cat "$pkg_zone") / 1000 )); (( t > maxtemp )) && maxtemp=$t; fi
    h=$(awk '/AnonHugePages/ {s += $2} END {print s+0}' /proc/$pid/smaps 2>/dev/null || echo 0)
    (( h > hugekb )) && hugekb=$h
    sleep 2
  done
  wait $pid || true
  c1=$(node_cpu_ticks); t1=$(date +%s.%N)
  node_pct=$(awk -v a="$c0" -v b="$c1" -v s="$t0" -v e="$t1" -v hz="$hz" 'BEGIN {printf "%.0f", (b - a) / hz / (e - s) * 100}')
  total=$(grep -oE 'total_hps=[0-9.]+' "$log" | cut -d= -f2)
  per=$(grep -oE 'per thread \[[^]]*\]' "$log" | sed 's/per thread //')
  printf '| %s | %d | %s | %s | %.3f | %s | %d °C | %d MiB | %s%% |\n' \
    "${LABEL:-$(basename "$BIN")}" "$n" "$NF" "${HP#--}" "${total:-0}" "$per" "$maxtemp" $((hugekb / 1024)) "$node_pct"
  rm -f "$log"
done
