#!/usr/bin/env bash
# T-08 hardware-counter profile of the benchmark (plan §12): runs
# `yacoin-cpuminer --benchmark` under `perf stat` (per process, counting starts
# after the warm-up) and, at the same time, a system-wide `perf stat` of the
# DRAM controller counters (uncore_imc). Needs perf with cap_perfmon or a low
# kernel.perf_event_paranoid (owner decision Q7); no sudo.
#
#   scripts/profile.sh [--bin build-dev/yacoin-cpuminer] [--seconds 180] [--warmup 30]
#                      [--nfactor 21] [--lanes L] [--extra "ARGS"] THREADS
#
# Prints the derived metrics as one markdown row:
#   rate, effective GHz (cycles / task-clock), L3-miss stall share
#   (cycle_activity.stalls_l3_miss / cycles), mean L2-miss demand-read latency
#   in ns (offcore_requests_outstanding.demand_data_rd /
#   offcore_requests.demand_data_rd, cycles converted with the effective
#   clock), DRAM read/write GB/s (uncore_imc, 64 B per count), TLB-walk share
#   (dtlb_load_misses.walk_active / cycles) and the TopdownL1 split
#   (system-wide, over the same window).
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=build-dev/yacoin-cpuminer
SECS=180
WARM=30
NF=21
LANES=""
EXTRA=""
while [[ $# -gt 1 ]]; do
  case "$1" in
    --bin) BIN=$2; shift 2 ;;
    --seconds) SECS=$2; shift 2 ;;
    --warmup) WARM=$2; shift 2 ;;
    --nfactor) NF=$2; shift 2 ;;
    --lanes) LANES=$2; shift 2 ;;
    --extra) EXTRA=$2; shift 2 ;;
    *) echo "unknown option $1" >&2; exit 1 ;;
  esac
done
N=$1
(( N >= 1 && N <= 8 )) || { echo "threads must be 1..8" >&2; exit 1; }
args=(--benchmark --threads "$N" --nfactor "$NF" --bench-seconds "$SECS" --bench-warmup "$WARM" --hugepages --nice 0)
[[ -n $LANES ]] && args+=(--lanes "$LANES")
# shellcheck disable=SC2206
[[ -n $EXTRA ]] && args+=($EXTRA)

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
EV_CORE='{cycles,offcore_requests_outstanding.demand_data_rd,offcore_requests.demand_data_rd,cycle_activity.stalls_l3_miss},dtlb_load_misses.walk_active,task-clock'
# The page-touching hash per thread runs before timing starts; give it a few
# seconds on top of the warm-up (it is ~1-2 s per lane at N-factor 21).
delay_ms=$(( (WARM + 5) * 1000 ))
window=$(( SECS - WARM - 10 ))
nice -n 10 perf stat -x, -o "$tmp/core.csv" -D "$delay_ms" -e "$EV_CORE" -- "$BIN" "${args[@]}" >"$tmp/miner.log" 2>&1 &
pid=$!
sleep $(( WARM + 5 ))
# System-wide: the DRAM counters exist only per package, and the TopdownL1
# metric events are not inherited by the miner's threads per process. The
# node is idle, so the miner dominates both.
perf stat -x, -o "$tmp/imc.csv" -a -e uncore_imc/data_reads/,uncore_imc/data_writes/ -M TopdownL1 -- sleep "$window" 2>/dev/null
wait $pid || true

val() {  # value of an event from a perf -x, CSV
  awk -F, -v e="$2" '$3 == e || $3 ~ "^"e"(:u)?$" {gsub(/ /, "", $1); print $1; exit}' "$1"
}
cyc=$(val "$tmp/core.csv" cycles)
occ=$(val "$tmp/core.csv" offcore_requests_outstanding.demand_data_rd)
req=$(val "$tmp/core.csv" offcore_requests.demand_data_rd)
stl=$(val "$tmp/core.csv" cycle_activity.stalls_l3_miss)
walk=$(val "$tmp/core.csv" dtlb_load_misses.walk_active)
tclk=$(val "$tmp/core.csv" task-clock)
rd=$(val "$tmp/imc.csv" uncore_imc/data_reads/)
wr=$(val "$tmp/imc.csv" uncore_imc/data_writes/)
rd_unit=$(awk -F, '$3 ~ /data_reads/ {print $2; exit}' "$tmp/imc.csv")
td=$(grep -E 'tma_' "$tmp/imc.csv" | awk -F, '{gsub(/ /, "", $6); sub(/^% */, "", $7); sub(/^tma_/, "", $7); if ($7 != "") printf "%s %s%%; ", $7, $6}')
rate=$(grep -oE 'total_hps=[0-9.]+' "$tmp/miner.log" | cut -d= -f2 || echo 0)

awk -v n="$N" -v l="${LANES:-1}" -v rate="$rate" -v cyc="$cyc" -v occ="$occ" -v req="$req" -v stl="$stl" -v walk="$walk" \
    -v tclk="$tclk" -v rd="$rd" -v wr="$wr" -v unit="$rd_unit" -v win="$window" -v td="$td" 'BEGIN {
  ghz = cyc / (tclk * 1e6)                     # task-clock is in ms
  lat = (req > 0) ? occ / req / ghz : 0       # ns
  mul = (unit == "MiB") ? 1048576 : 64        # perf may report MiB or counts
  printf "| %d | %d | %.3f | %.2f | %.0f%% | %.0f | %.2f | %.2f | %.1f%% | %s |\n", n, l, rate, ghz, stl / cyc * 100, lat,
         rd * mul / win / 1e9, wr * mul / win / 1e9, walk / cyc * 100, td
}'
