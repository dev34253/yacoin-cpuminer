// yacoin-cpuminer: thread-safe, timestamped logging. MIT licence.
#include "util/log.h"

#include <cstdio>
#include <ctime>
#include <mutex>

namespace yac {

static std::mutex g_log_mu;

static void emit(FILE* f, const char* prefix, const std::string& msg)
{
    char ts[32];
    std::time_t t = std::time(nullptr);
    std::tm tm;
    localtime_r(&t, &tm);
    std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);
    std::lock_guard<std::mutex> l(g_log_mu);
    std::fprintf(f, "%s %s%s\n", ts, prefix, msg.c_str());
    std::fflush(f);
}

void log_info(const std::string& msg) { emit(stdout, "", msg); }
void log_warn(const std::string& msg) { emit(stderr, "WARNING: ", msg); }
void log_error(const std::string& msg) { emit(stderr, "ERROR: ", msg); }

std::string fmt_double(double v, int decimals)
{
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", decimals, v);
    return buf;
}

}  // namespace yac
