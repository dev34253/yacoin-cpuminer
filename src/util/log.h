// yacoin-cpuminer: thread-safe, timestamped logging to stdout/stderr. MIT licence.
#pragma once

#include <string>

namespace yac {

void log_info(const std::string& msg);   // stdout
void log_warn(const std::string& msg);   // stderr, "WARNING: "
void log_error(const std::string& msg);  // stderr, "ERROR: "

// "12.3" style formatting helper.
std::string fmt_double(double v, int decimals);

}  // namespace yac
