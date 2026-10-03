#!/usr/bin/env bash
# Read-only JSON-RPC call to the node configured in the miner config.
#
#   scripts/rpc-readonly.sh <method> [json-params]     e.g.  getblockheader '["<hash>", false]'
#
# Config: $YAC_MINER_CONF or ~/.config/yacoin-cpuminer/miner.conf (rpchost,
# rpcport, rpcuser, rpcpassword). The credentials go to curl through
# `--config -` on stdin, never on the command line, and are never printed.
# Only read-only methods are allowed; everything else is refused.
set -euo pipefail

CONF="${YAC_MINER_CONF:-$HOME/.config/yacoin-cpuminer/miner.conf}"
method="${1:?usage: $0 <method> [json-params]}"
params="${2:-[]}"

case "$method" in
  getblockcount|getblockhash|getblockheader|getblock|getbestblockhash|getmininginfo|getinfo|getconnectioncount|getpeerinfo|getdifficulty) ;;
  *) echo "rpc-readonly: method '$method' is not on the read-only allowlist" >&2; exit 2 ;;
esac

get() { sed -n -E "s/^[[:space:]]*$1[[:space:]]*=[[:space:]]*//p" "$CONF" | tail -1; }
host="$(get rpchost)"; port="$(get rpcport)"; user="$(get rpcuser)"; pass="$(get rpcpassword)"
host="${host:-127.0.0.1}"; port="${port:-7687}"
[[ -n "$user" && -n "$pass" ]] || { echo "rpc-readonly: rpcuser/rpcpassword missing in $CONF" >&2; exit 1; }

# curl config syntax: a quoted string with \" and \\ escaped.
esc() { local s=${1//\\/\\\\}; printf '%s' "${s//\"/\\\"}"; }
body="{\"jsonrpc\":\"1.0\",\"id\":\"ro\",\"method\":\"$method\",\"params\":$params}"
printf 'user = "%s:%s"\n' "$(esc "$user")" "$(esc "$pass")" |
  curl -sS --config - --max-time 60 -H 'content-type: text/plain;' \
       --data-binary "$body" "http://$host:$port/"
echo
