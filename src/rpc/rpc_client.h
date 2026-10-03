// yacoin-cpuminer: JSON-RPC 1.0 client over HTTP with basic auth (libcurl). MIT licence.
#pragma once

#include <stdexcept>
#include <string>

#include "json.hpp"
#include "rpc/config.h"

namespace yac {

// The node could not be reached or the HTTP exchange failed (connection
// refused, timeout, malformed reply).
struct RpcTransportError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// HTTP 401 (wrong rpcuser/rpcpassword) or 403 (rpcallowip refuses us).
struct RpcAuthError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// The node answered with a JSON-RPC error object.
struct RpcError : std::runtime_error {
    int code;
    RpcError(int c, const std::string& msg) : std::runtime_error(msg), code(c) {}
};

// JSON-RPC error codes used by yacoind's getwork (src/rpc/protocol.h).
constexpr int kRpcClientNotConnected = -9;       // "Yacoin is not connected!"
constexpr int kRpcClientInInitialDownload = -10;  // "Yacoin is downloading blocks..."
constexpr int kRpcWalletLocked = -100;            // getwork submit: "Unable to sign block, wallet locked?"
constexpr int kRpcMethodNotFound = -32601;

// Not thread-safe: use one client per thread. The password is handed to
// libcurl in-process and never appears in logs or error messages.
class RpcClient {
public:
    explicit RpcClient(RpcSettings settings);
    ~RpcClient();
    RpcClient(const RpcClient&) = delete;
    RpcClient& operator=(const RpcClient&) = delete;

    nlohmann::json call(const std::string& method, const nlohmann::json& params = nlohmann::json::array());

    const RpcSettings& settings() const { return s_; }

private:
    RpcSettings s_;
    void* curl_;  // CURL*
    long next_id_ = 1;
};

// Call once from main before any thread uses RpcClient.
void rpc_global_init();

}  // namespace yac
