// yacoin-cpuminer: config parsing and the JSON-RPC client against a fake server. MIT licence.
#include <cstdio>
#include <fstream>

#include <sys/stat.h>

#include "testing.h"

#include "fake_http.h"
#include "rpc/config.h"
#include "rpc/node_api.h"
#include "rpc/rpc_client.h"

using namespace yac;

static std::string tmpfile_with(const std::string& content, mode_t mode)
{
    char name[] = "/tmp/yac-test-conf-XXXXXX";
    int fd = mkstemp(name);
    (void)!write(fd, content.data(), content.size());
    close(fd);
    chmod(name, mode);
    return name;
}

TEST(read_miner_conf_and_yacoin_conf)
{
    std::string p = tmpfile_with("# comment\nrpchost = 127.0.0.2\nrpcport=1234\nrpcuser=u\nrpcpassword=p=w#x\nthreads=7\n", 0600);
    auto kv = read_kv_file(p);
    RpcSettings s;
    apply_rpc_keys(kv, s);
    CHECK_EQ(s.host, std::string("127.0.0.2"));
    CHECK_EQ(s.port, 1234);
    CHECK_EQ(s.user, std::string("u"));
    CHECK_EQ(s.password, std::string("p=w#x"));  // only the first '=' splits; '#' inside a value is kept
    CHECK_EQ(kv["threads"], std::string("7"));
    CHECK(s.describe().find("p=w") == std::string::npos);  // never shows the password
    CHECK(permission_warning(p).empty());
    std::remove(p.c_str());

    std::string y = tmpfile_with("rpcconnect=10.0.0.1\nrpcuser=a\nrpcpassword=b\nrpcport=7687\n[test]\n", 0644);
    RpcSettings s2;
    apply_rpc_keys(read_kv_file(y), s2);
    CHECK_EQ(s2.host, std::string("10.0.0.1"));
    CHECK(!permission_warning(y).empty());
    std::remove(y.c_str());
    CHECK_THROWS(read_kv_file("/nonexistent/miner.conf"));
}

TEST(yacoin_conf_is_read_like_the_node)
{
    // boost config_file_iterator (yacoin src/util.cpp ReadConfigFile): '#'
    // starts a comment anywhere, the first value wins, [section] keys are not
    // plain keys.
    std::string y = tmpfile_with("rpcuser=first\nrpcuser=second\nrpcpassword=p=w#comment\n"
                                 "rpcport=7687 # mainnet\n[test]\nrpcport=17687\n", 0600);
    RpcSettings s;
    apply_rpc_keys(read_kv_file(y, ConfStyle::Node), s);
    CHECK_EQ(s.user, std::string("first"));
    CHECK_EQ(s.password, std::string("p=w"));
    CHECK_EQ(s.port, 7687);
    std::remove(y.c_str());

    std::string bad = tmpfile_with("rpcport=77x\n", 0600);
    RpcSettings s3;
    bool named = false;
    try {
        apply_rpc_keys(read_kv_file(bad), s3);
    } catch (const std::invalid_argument& e) {
        named = std::string(e.what()).find("rpcport") != std::string::npos;
    }
    CHECK(named);
    std::remove(bad.c_str());
}

static RpcSettings settings_for(int port)
{
    RpcSettings s;
    s.host = "127.0.0.1";
    s.port = port;
    s.user = "alice";
    s.password = "secret-pw";
    s.timeout_s = 5;
    return s;
}

TEST(rpc_call_success_and_basic_auth)
{
    FakeHttpServer srv([](const std::string&) {
        return std::make_pair(200, std::string(R"({"result":"00ab","error":null,"id":1})"));
    });
    RpcClient c(settings_for(srv.port()));
    CHECK_EQ(c.call("getbestblockhash").get<std::string>(), std::string("00ab"));
    auto reqs = srv.requests();
    CHECK_EQ(reqs.size(), size_t(1));
    // base64("alice:secret-pw") = YWxpY2U6c2VjcmV0LXB3
    CHECK(reqs[0].find("Authorization: Basic YWxpY2U6c2VjcmV0LXB3") != std::string::npos);
    CHECK(reqs[0].find("\"method\":\"getbestblockhash\"") != std::string::npos);
}

TEST(rpc_errors_are_classified)
{
    int code = 0;
    FakeHttpServer srv([&](const std::string&) {
        if (code == 401) return std::make_pair(401, std::string());
        return std::make_pair(500, R"({"result":null,"error":{"code":)" + std::to_string(code) +
                                       R"(,"message":"m"},"id":1})");
    });
    RpcNodeApi api(settings_for(srv.port()));
    auto failure_of = [&](int c) {
        code = c;
        try {
            api.get_work();
        } catch (const std::exception& e) {
            // Error messages never contain the password.
            CHECK(std::string(e.what()).find("secret-pw") == std::string::npos);
            return classify_rpc_failure(e);
        }
        return RpcFailure::Other;
    };
    CHECK(failure_of(-9) == RpcFailure::Transient);    // not connected (F8)
    CHECK(failure_of(-10) == RpcFailure::Transient);   // initial download (F8)
    CHECK(failure_of(-28) == RpcFailure::Transient);   // warm-up
    CHECK(failure_of(-100) == RpcFailure::WalletLocked);
    CHECK(failure_of(-8) == RpcFailure::Other);
    CHECK(failure_of(401) == RpcFailure::Auth);
}

TEST(rpc_http_edge_cases)
{
    int mode = 0;
    FakeHttpServer srv([&](const std::string&) {
        switch (mode) {
        case 0: return std::make_pair(404, std::string(R"({"result":null,"error":{"code":-32601,"message":"Method not found"},"id":1})"));
        case 1: return std::make_pair(503, std::string("<html>busy</html>"));
        case 2: return std::make_pair(200, std::string(R"({"result":null,"error":{"code":-5,"message":"x"},"id":1})"));
        case 3: return std::make_pair(403, std::string());
        default: return std::make_pair(500, std::string(R"({"result":null,"error":"plain string","id":1})"));
        }
    });
    RpcClient c(settings_for(srv.port()));
    auto outcome = [&](int m) -> std::string {
        mode = m;
        try {
            c.call("getwork");
        } catch (const RpcError& e) {
            return "rpc" + std::to_string(e.code);
        } catch (const RpcTransportError&) {
            return "transport";
        } catch (const RpcAuthError& e) {
            return std::string("auth:") + e.what();
        }
        return "ok";
    };
    CHECK_EQ(outcome(0), std::string("rpc-32601"));  // e.g. wallet disabled: getwork not found
    CHECK_EQ(outcome(1), std::string("transport"));
    CHECK_EQ(outcome(2), std::string("rpc-5"));
    CHECK(outcome(3).find("rpcallowip") != std::string::npos);
    CHECK_EQ(outcome(4), std::string("rpc0"));
}

TEST(rpc_transport_error_when_node_down)
{
    int port;
    {
        FakeHttpServer srv([](const std::string&) { return std::make_pair(200, std::string("{}")); });
        port = srv.port();
    }
    RpcClient c(settings_for(port));
    bool transient = false;
    try {
        c.call("getbestblockhash");
    } catch (const std::exception& e) {
        transient = classify_rpc_failure(e) == RpcFailure::Transient;
    }
    CHECK(transient);
}

TEST(submit_parses_boolean)
{
    FakeHttpServer srv([](const std::string& req) {
        bool ok = req.find("\"params\":[\"aa\"]") != std::string::npos;
        return std::make_pair(200, std::string(R"({"result":)") + (ok ? "true" : "false") + R"(,"error":null,"id":1})");
    });
    RpcNodeApi api(settings_for(srv.port()));
    CHECK(api.submit_work("aa"));
    CHECK(!api.submit_work("bb"));
}

int main()
{
    rpc_global_init();
    return yac_test::run_all();
}
