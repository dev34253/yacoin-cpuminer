// yacoin-cpuminer: JSON-RPC client over HTTP with basic auth (libcurl). MIT licence.
#include "rpc/rpc_client.h"

#include <curl/curl.h>

namespace yac {

void rpc_global_init() { curl_global_init(CURL_GLOBAL_DEFAULT); }

static size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

RpcClient::RpcClient(RpcSettings settings) : s_(std::move(settings)), curl_(curl_easy_init())
{
    if (!curl_) throw std::runtime_error("curl_easy_init failed");
}

RpcClient::~RpcClient() { curl_easy_cleanup(static_cast<CURL*>(curl_)); }

nlohmann::json RpcClient::call(const std::string& method, const nlohmann::json& params)
{
    CURL* c = static_cast<CURL*>(curl_);
    nlohmann::json req = {{"jsonrpc", "1.0"}, {"id", next_id_++}, {"method", method}, {"params", params}};
    std::string body = req.dump();
    std::string reply;
    std::string url = s_.url();

    curl_easy_reset(c);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(c, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(c, CURLOPT_USERNAME, s_.user.c_str());
    curl_easy_setopt(c, CURLOPT_PASSWORD, s_.password.c_str());
    curl_easy_setopt(c, CURLOPT_TIMEOUT, s_.timeout_s);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_NOPROXY, "*");  // never send credentials through a proxy
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &reply);
    struct curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);

    CURLcode rc = curl_easy_perform(c);
    long http = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(headers);
    // Clear the credentials from the handle before anything else can see it.
    curl_easy_setopt(c, CURLOPT_USERNAME, "");
    curl_easy_setopt(c, CURLOPT_PASSWORD, "");

    if (rc != CURLE_OK)
        throw RpcTransportError(method + ": " + curl_easy_strerror(rc) + " (" + s_.host + ":" +
                                std::to_string(s_.port) + ")");
    if (http == 401) throw RpcAuthError(method + ": HTTP 401, check rpcuser/rpcpassword");
    if (http == 403) throw RpcAuthError(method + ": HTTP 403, the node's rpcallowip does not allow this client");

    // Bitcoin-style servers send RPC errors with HTTP 404/500 and a JSON body.
    nlohmann::json r;
    try {
        r = nlohmann::json::parse(reply);
    } catch (const std::exception&) {
        throw RpcTransportError(method + ": HTTP " + std::to_string(http) + ", reply is not JSON");
    }
    if (!r.is_object()) throw RpcTransportError(method + ": reply is not a JSON object");
    auto err = r.find("error");
    if (err != r.end() && !err->is_null()) {
        int code = 0;
        std::string msg = "unknown error";
        if (err->is_object()) {
            auto c = err->find("code");
            if (c != err->end() && c->is_number_integer()) code = c->get<int>();
            auto m = err->find("message");
            if (m != err->end() && m->is_string()) msg = m->get<std::string>();
        } else {
            msg = err->dump();
        }
        throw RpcError(code, method + ": " + msg + " (code " + std::to_string(code) + ")");
    }
    if (http != 200) throw RpcTransportError(method + ": HTTP " + std::to_string(http));
    auto res = r.find("result");
    if (res == r.end()) throw RpcTransportError(method + ": reply has no result");
    return *res;
}

}  // namespace yac
