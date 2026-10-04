#!/usr/bin/env bash
# Benchmark matrix (plan §12 method): H/s at an N-factor for several
# configurations, run in interleaved repeats (A B C A B C ...), under nice 10.
# No node RPC is used. Each run discards its first --warmup seconds (laptop
# turbo/PL2) and samples the CPU clock (mean scaling_cur_freq of the THREADS
# busiest CPUs; perf's cycles/task-clock in scripts/profile.sh is more exact),
# the package temperature, the miner's huge-page use and the mainnet node's
# CPU use. The machine state (AC power, governor, EPP, THP) is printed first.
#
#   scripts/bench.sh [--bin build-dev/yacoin-cpuminer] [--seconds 180] [--warmup 30]
#                    [--repeats 3] [--nfactor 21] [--no-hugepages] CONFIG...
#
# CONFIG is THREADS[:LANES[:ARGS]]; ARGS are extra miner options separated by
# commas (e.g. "7:2:--affinity=spread"); the token bin=PATH inside ARGS runs
# that configuration with another binary. LANES is passed as --lanes only when
# given. Prints one markdown row per run, then a summary row per
# configuration (median and min-max of the total H/s). Never more than 8
# threads, and lanes x threads x scratch must stay within 12 GiB (plan §12).
set -euo pipefail
cd "$(dirname "$0")/.."
BIN=build/yacoin-cpuminer
SECS=180
WARM=30
REPS=3
NF=21
HP=--hugepages
CONFIGS=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --bin) BIN=$2; shift 2 ;;
    --seconds) SECS=$2; shift 2 ;;
    --warmup) WARM=$2; shift 2 ;;
    --repeats) REPS=$2; shift 2 ;;
    --nfactor) NF=$2; shift 2 ;;
    --no-hugepages) HP=--no-hugepages; shift ;;
    *) CONFIGS+=("$1"); shift ;;
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

mean_mhz() {  # mean current clock of the $1 fastest CPUs (the busy ones; idle CPUs sit at the minimum)
  cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq 2>/dev/null | sort -rn | head -n "$1" |
    awk '{s += $1; n++} END {if (n) printf "%.0f", s / n / 1000; else print 0}'
}

ac=$(cat /sys/class/power_supply/AC/online 2>/dev/null || echo "?")
gov=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo "?")
epp=$(cat /sys/devices/system/cpu/cpu0/cpufreq/energy_performance_preference 2>/dev/null || echo "?")
thp=$(sed 's/.*\[\(.*\)\].*/\1/' /sys/kernel/mm/transparent_hugepage/enabled 2>/dev/null || echo "?")
echo "# $(date -Is) AC online=$ac governor=$gov EPP=$epp THP=$thp; ${SECS} s per run, first ${WARM} s discarded, $REPS interleaved repeats, N-factor $NF, $HP"
echo "# mainnet miner running: $(pgrep -x yacoin-cpuminer >/dev/null && echo YES || echo no)"
echo
echo "| Config | Rep | Total H/s | Per thread | Mean MHz | Temp mean/max | Huge MiB | Node CPU |"
echo "|---|---|---|---|---|---|---|---|"

declare -A RESULTS
hz=$(getconf CLK_TCK)
for ((r = 1; r <= REPS; r++)); do
  for cfg in "${CONFIGS[@]}"; do
    IFS=: read -r n lanes args <<<"$cfg"
    (( n >= 1 && n <= 8 )) || { echo "threads must be 1..8" >&2; exit 1; }
    bin=$BIN
    extra=()
    if [[ -n ${lanes:-} ]]; then
      if (( NF >= 21 && lanes * n > 24 )); then echo "$cfg: lanes x threads x 0.5 GiB > 12 GiB" >&2; exit 1; fi
      extra+=(--lanes "$lanes")
    fi
    if [[ -n ${args:-} ]]; then
      IFS=, read -ra toks <<<"$args"
      for t in "${toks[@]}"; do
        if [[ $t == bin=* ]]; then bin=${t#bin=}
        elif [[ $t == --*=* ]]; then extra+=("${t%%=*}" "${t#*=}")  # --opt=value -> --opt value
        else extra+=("$t"); fi
      done
    fi
    log=$(mktemp)
    c0=$(node_cpu_ticks); t0=$(date +%s.%N)
    nice -n 10 "$bin" --benchmark --threads "$n" --nfactor "$NF" --bench-seconds "$SECS" --bench-warmup "$WARM" \
      "$HP" --nice 0 "${extra[@]}" >"$log" 2>&1 &
    pid=$!
    # Sampling starts when the timed part starts (after the per-thread
    # page-touching hash) plus the warm-up.
    while kill -0 $pid 2>/dev/null && ! grep -q 'benchmark: timing' "$log" 2>/dev/null; do sleep 0.5; done
    tstart=$(date +%s)
    maxtemp=0; sumtemp=0; ntemp=0; summhz=0; nmhz=0; hugekb=0
    while kill -0 $pid 2>/dev/null; do
      if (( $(date +%s) - tstart >= WARM )); then
        if [[ -n $pkg_zone ]]; then
          t=$(( $(cat "$pkg_zone") / 1000 )); (( t > maxtemp )) && maxtemp=$t
          sumtemp=$((sumtemp + t)); ntemp=$((ntemp + 1))
        fi
        summhz=$((summhz + $(mean_mhz "$n"))); nmhz=$((nmhz + 1))
      fi
      h=$(awk '/AnonHugePages/ {s += $2} END {print s+0}' /proc/$pid/smaps 2>/dev/null || echo 0)
      (( h > hugekb )) && hugekb=$h
      sleep 2
    done
    wait $pid || true
    c1=$(node_cpu_ticks); t1=$(date +%s.%N)
    node_pct=$(awk -v a="$c0" -v b="$c1" -v s="$t0" -v e="$t1" -v hz="$hz" 'BEGIN {printf "%.0f", (b - a) / hz / (e - s) * 100}')
    total=$(grep -oE 'total_hps=[0-9.]+' "$log" | cut -d= -f2 || true)
    per=$(grep -oE 'per thread \[[^]]*\]' "$log" | sed 's/per thread //' || true)
    if [[ -z $total ]]; then echo "| $cfg | $r | FAILED: $(tail -n 2 "$log" | tr '\n' ' ') |" ; rm -f "$log"; continue; fi
    printf '| %s | %d | %.3f | %s | %d | %d/%d °C | %d | %s%% |\n' "$cfg" "$r" "$total" "$per" \
      $(( nmhz ? summhz / nmhz : 0 )) $(( ntemp ? sumtemp / ntemp : 0 )) "$maxtemp" $((hugekb / 1024)) "$node_pct"
    RESULTS[$cfg]+="$total "
    rm -f "$log"
  done
done

echo
echo "| Config | Runs | Median H/s | Min-max H/s | Spread % |"
echo "|---|---|---|---|---|"
for cfg in "${CONFIGS[@]}"; do
  echo "${RESULTS[$cfg]:-}" | tr ' ' '\n' | grep -v '^$' | sort -g | awk -v c="$cfg" '
    {v[NR] = $1} END {
      if (NR == 0) {print "| " c " | 0 | - | - | - |"; exit}
      m = (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2
      printf "| %s | %d | %.3f | %.3f-%.3f | %.1f |\n", c, NR, m, v[1], v[NR], (v[NR] - v[1]) / m * 100
    }'
done
