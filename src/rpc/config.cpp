// yacoin-cpuminer: RPC and miner settings from config files. MIT licence.
#include "rpc/config.h"

#include <sys/stat.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>

namespace yac {

std::string default_miner_conf_path()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return std::string(xdg) + "/yacoin-cpuminer/miner.conf";
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.config/yacoin-cpuminer/miner.conf";
}

static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::map<std::string, std::string> read_kv_file(const std::string& path, ConfStyle style)
{
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot read config file " + path);
    std::map<std::string, std::string> kv;
    std::string line;
    bool in_section = false;
    while (std::getline(f, line)) {
        if (style == ConfStyle::Node) {
            size_t hash = line.find('#');
            if (hash != std::string::npos) line.erase(hash);
        }
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        if (t[0] == '[') {
            in_section = true;
            continue;
        }
        if (style == ConfStyle::Node && in_section) continue;
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(t.substr(0, eq));
        if (style == ConfStyle::Node && kv.count(key)) continue;  // first wins
        kv[key] = trim(t.substr(eq + 1));
    }
    return kv;
}

std::string RpcSettings::url() const { return "http://" + host + ":" + std::to_string(port) + "/"; }

std::string RpcSettings::describe() const
{
    return (user.empty() ? std::string("<no user>") : user) + "@" + host + ":" + std::to_string(port);
}

void apply_rpc_keys(const std::map<std::string, std::string>& kv, RpcSettings& s)
{
    auto get = [&](const char* k) -> const std::string* {
        auto it = kv.find(k);
        return it == kv.end() ? nullptr : &it->second;
    };
    if (auto v = get("rpcconnect")) s.host = *v;
    if (auto v = get("rpchost")) s.host = *v;
    auto number = [](const char* key, const std::string& v, long lo, long hi) {
        size_t pos = 0;
        long n = 0;
        try {
            n = std::stol(v, &pos);
        } catch (...) {
            pos = 0;
        }
        if (pos == 0 || pos != v.size() || n < lo || n > hi)
            throw std::invalid_argument(std::string("config key ") + key + ": expected a number " +
                                        std::to_string(lo) + ".." + std::to_string(hi) + ", got '" + v + "'");
        return n;
    };
    if (auto v = get("rpcport")) s.port = static_cast<int>(number("rpcport", *v, 1, 65535));
    if (auto v = get("rpcuser")) s.user = *v;
    if (auto v = get("rpcpassword")) s.password = *v;
    if (auto v = get("rpctimeout")) s.timeout_s = number("rpctimeout", *v, 1, 3600);
}

std::string permission_warning(const std::string& path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return "";
    if (st.st_mode & (S_IRWXG | S_IRWXO))
        return "config file " + path + " is accessible by group/others; it holds the RPC password: chmod 600 it";
    return "";
}

}  // namespace yac
