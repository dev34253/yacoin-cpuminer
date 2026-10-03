// yacoin-cpuminer: RPC and miner settings from config files. MIT licence.
#pragma once

#include <map>
#include <optional>
#include <string>

namespace yac {

// Default miner config (owner decision Q3): a private copy of the node's
// rpcuser/rpcpassword/rpcport, mode 600.
std::string default_miner_conf_path();

enum class ConfStyle {
    // miner.conf: lines starting with '#' are comments; a '#' inside a value
    // is kept; the last occurrence of a key wins.
    Miner,
    // yacoin.conf, read like the node does (boost config_file_iterator in
    // yacoin src/util.cpp ReadConfigFile): '#' starts a comment anywhere in a
    // line, the first occurrence of a key wins, keys after a [section] header
    // belong to that section and are ignored.
    Node,
};

// key=value lines; whitespace around key and value is trimmed. Throws
// std::runtime_error if the file cannot be read.
std::map<std::string, std::string> read_kv_file(const std::string& path, ConfStyle style = ConfStyle::Miner);

struct RpcSettings {
    std::string host = "127.0.0.1";
    int port = 7687;  // yacoind mainnet RPC port
    std::string user;
    std::string password;  // never printed or logged
    long timeout_s = 120;  // a submit computes a 512 MiB hash on the node (plan F11)

    std::string url() const;
    // For log lines: user@host:port, never the password.
    std::string describe() const;
};

// Applies keys from a miner.conf (rpchost, rpcport, rpcuser, rpcpassword) or a
// yacoin.conf (rpcconnect, rpcport, rpcuser, rpcpassword) to `s`.
void apply_rpc_keys(const std::map<std::string, std::string>& kv, RpcSettings& s);

// Returns a warning if the file is readable by group or others, else empty.
std::string permission_warning(const std::string& path);

}  // namespace yac
