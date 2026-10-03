// yacoin-cpuminer: the node calls the miner needs. MIT licence.
#include "rpc/node_api.h"

namespace yac {

std::string RpcNodeApi::best_block_hash()
{
    auto r = rpc_.call("getbestblockhash");
    if (!r.is_string()) throw RpcTransportError("getbestblockhash: reply is not a string");
    return r.get<std::string>();
}

Work RpcNodeApi::get_work()
{
    auto r = rpc_.call("getwork");
    if (!r.is_object() || !r.contains("data") || !r.contains("target") || !r["data"].is_string() ||
        !r["target"].is_string())
        throw RpcTransportError("getwork: reply has no data/target strings");
    return decode_getwork(r["data"].get<std::string>(), r["target"].get<std::string>());
}

bool RpcNodeApi::submit_work(const std::string& data_hex)
{
    auto r = rpc_.call("getwork", nlohmann::json::array({data_hex}));
    if (!r.is_boolean()) throw RpcTransportError("getwork submit: reply is not a boolean");
    return r.get<bool>();
}

MiningInfo RpcNodeApi::mining_info()
{
    auto r = rpc_.call("getmininginfo");
    if (!r.is_object()) throw RpcTransportError("getmininginfo: reply is not an object");
    MiningInfo m;
    m.blocks = r.value("blocks", int64_t(0));
    m.nfactor = r.value("Nfactor", 0u);
    m.n = r.value("N", uint64_t(0));
    return m;
}

NodeInfo RpcNodeApi::node_info()
{
    auto r = rpc_.call("getinfo");
    if (!r.is_object()) throw RpcTransportError("getinfo: reply is not an object");
    NodeInfo n;
    n.version = r.value("version", std::string());
    n.blocks = r.value("blocks", int64_t(0));
    n.connections = r.value("connections", 0);
    n.keypoolsize = r.value("keypoolsize", int64_t(-1));
    if (r.contains("unlocked_until") && r["unlocked_until"].is_number())
        n.unlocked_until = r["unlocked_until"].get<int64_t>();
    return n;
}

RpcFailure classify_rpc_failure(const std::exception& e)
{
    if (dynamic_cast<const RpcAuthError*>(&e)) return RpcFailure::Auth;
    if (dynamic_cast<const RpcTransportError*>(&e)) return RpcFailure::Transient;
    if (auto* r = dynamic_cast<const RpcError*>(&e)) {
        switch (r->code) {
        case kRpcClientNotConnected:
        case kRpcClientInInitialDownload:
        case kRpcInWarmup:
            return RpcFailure::Transient;
        case kRpcWalletLocked:
            return RpcFailure::WalletLocked;
        default:
            return RpcFailure::Other;
        }
    }
    return RpcFailure::Other;
}

}  // namespace yac
