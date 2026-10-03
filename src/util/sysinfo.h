// yacoin-cpuminer: memory information from /proc/meminfo. MIT licence.
#pragma once

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace yac {

// MemAvailable in bytes, or 0 if unknown.
inline uint64_t mem_available_bytes()
{
    std::ifstream f("/proc/meminfo");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("MemAvailable:", 0) == 0) {
            std::istringstream is(line.substr(13));
            uint64_t kb = 0;
            is >> kb;
            return kb * 1024;
        }
    }
    return 0;
}

}  // namespace yac
