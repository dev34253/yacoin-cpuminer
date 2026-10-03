#!/usr/bin/env bash
# Configure, build and (unless --no-test) test yacoin-cpuminer.
#
#   scripts/build.sh [--build-dir DIR] [--no-test] [extra cmake -D options...]
#
# Uses the system cmake and libcurl headers when they are installed
# (sudo apt install cmake libcurl4-openssl-dev). If they are not, it falls back to
#   - cmake from PyPI via `uvx --from cmake` (no sudo), and
#   - libcurl4-openssl-dev unpacked with `apt-get download` + `dpkg -x` into
#     $YAC_DEPS (default ~/.cache/yacoin-cpuminer-deps), linked against the
#     system's runtime libcurl.so.4. Nothing is installed system-wide.
# Builds run under `nice -n 10` with at most 4 parallel jobs.
set -euo pipefail

cd "$(dirname "$0")/.."
BUILD_DIR=build
RUN_TESTS=1
EXTRA=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --build-dir) BUILD_DIR="$2"; shift 2 ;;
    --no-test) RUN_TESTS=0; shift ;;
    *) EXTRA+=("$1"); shift ;;
  esac
done

if command -v cmake >/dev/null 2>&1; then
  CMAKE=(cmake); CTEST=(ctest)
elif command -v uvx >/dev/null 2>&1; then
  CMAKE=(uvx --from cmake cmake); CTEST=(uvx --from cmake ctest)
else
  echo "cmake not found. Install it: sudo apt install cmake" >&2
  exit 1
fi

CURL_ARGS=()
if [[ ! -f /usr/include/x86_64-linux-gnu/curl/curl.h && ! -f /usr/include/curl/curl.h ]]; then
  YAC_DEPS="${YAC_DEPS:-$HOME/.cache/yacoin-cpuminer-deps}"
  INC="$YAC_DEPS/root/usr/include/x86_64-linux-gnu"
  if [[ ! -f "$INC/curl/curl.h" ]]; then
    echo "libcurl headers missing; unpacking libcurl4-openssl-dev into $YAC_DEPS (no sudo)" >&2
    ver="$(dpkg-query -W -f='${Version}' libcurl4t64 2>/dev/null || dpkg-query -W -f='${Version}' libcurl4)"
    mkdir -p "$YAC_DEPS"
    (cd "$YAC_DEPS" && apt-get download "libcurl4-openssl-dev=$ver" && dpkg -x libcurl4-openssl-dev_*.deb root)
  fi
  CURL_ARGS=(-DCURL_INCLUDE_DIR="$INC" -DCURL_LIBRARY=/usr/lib/x86_64-linux-gnu/libcurl.so.4)
fi

nice -n 10 "${CMAKE[@]}" -B "$BUILD_DIR" "${CURL_ARGS[@]}" "${EXTRA[@]}"
nice -n 10 "${CMAKE[@]}" --build "$BUILD_DIR" -j4
if [[ $RUN_TESTS == 1 ]]; then
  nice -n 10 "${CTEST[@]}" --test-dir "$BUILD_DIR" --output-on-failure
fi
