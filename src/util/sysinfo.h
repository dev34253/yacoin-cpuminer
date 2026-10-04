// yacoin-cpuminer: memory and CPU topology information (/proc, /sys). MIT licence.
#pragma once

#include <sched.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

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

// Largest CPU number accepted (glibc's fixed cpu_set_t holds CPU_SETSIZE = 1024).
constexpr int kMaxCpu = 1023;

// Parses a sysfs CPU list such as "0,4" or "0-1,8". Malformed parts and CPU
// numbers outside 0..kMaxCpu are skipped.
inline std::vector<int> parse_cpu_list(const std::string& s)
{
    std::vector<int> out;
    std::stringstream ss(s);
    std::string part;
    while (std::getline(ss, part, ',')) {
        if (part.empty() || part.find_first_not_of("0123456789-\n ") != std::string::npos) continue;
        size_t dash = part.find('-');
        try {
            int a = std::stoi(part.substr(0, dash));
            int b = dash == std::string::npos ? a : std::stoi(part.substr(dash + 1));
            if (a < 0 || b > kMaxCpu || a > b) continue;
            for (int c = a; c <= b; ++c) out.push_back(c);
        } catch (const std::logic_error&) {  // invalid_argument, out_of_range
        }
    }
    return out;
}

// SMT sibling groups (one per physical core), each sorted, ordered by their
// first CPU. `siblings` maps a CPU number to its thread_siblings_list text.
// Only CPUs in `usable` (the online CPUs the process may run on; empty = no
// filter) are kept, each CPU in one group only; empty groups are dropped.
inline std::vector<std::vector<int>> core_groups(const std::map<int, std::string>& siblings,
                                                 const std::set<int>& usable = {})
{
    auto ok = [&](int c) { return usable.empty() || usable.count(c) != 0; };
    std::set<std::vector<int>> groups;
    for (auto& [cpu, list] : siblings) {
        std::vector<int> g = parse_cpu_list(list);
        if (g.empty()) g = {cpu};
        std::sort(g.begin(), g.end());
        groups.insert(g);
    }
    std::vector<std::vector<int>> v;
    std::set<int> placed;
    for (auto& g : groups) {
        std::vector<int> kept;
        for (int c : g)
            if (ok(c) && placed.insert(c).second) kept.push_back(c);
        if (!kept.empty()) v.push_back(kept);
    }
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.front() < b.front(); });
    return v;
}

// The order in which worker threads are pinned (T-11): "compact" fills one
// core's SMT siblings before the next core; "spread" takes the first thread
// of every core, then the second ones. Worker i gets order[i % size].
inline std::vector<int> cpu_order(const std::vector<std::vector<int>>& cores, bool spread)
{
    std::vector<int> out;
    if (!spread) {
        for (auto& g : cores) out.insert(out.end(), g.begin(), g.end());
        return out;
    }
    size_t depth = 0;
    for (auto& g : cores) depth = std::max(depth, g.size());
    for (size_t t = 0; t < depth; ++t)
        for (auto& g : cores)
            if (t < g.size()) out.push_back(g[t]);
    return out;
}

// thread_siblings_list of every online CPU from /sys (empty if unreadable).
inline std::map<int, std::string> read_cpu_siblings()
{
    std::map<int, std::string> m;
    std::ifstream on("/sys/devices/system/cpu/online");
    std::string online;
    std::getline(on, online);
    for (int cpu : parse_cpu_list(online)) {
        std::ifstream f("/sys/devices/system/cpu/cpu" + std::to_string(cpu) + "/topology/thread_siblings_list");
        std::string s;
        if (std::getline(f, s)) m[cpu] = s;
    }
    return m;
}

// Online CPUs that this process may run on (sched_getaffinity, so taskset and
// cgroup cpusets are respected). Empty if unknown.
inline std::set<int> usable_cpus()
{
    std::set<int> out;
    cpu_set_t mask;
    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof mask, &mask) != 0) return out;
    std::ifstream on("/sys/devices/system/cpu/online");
    std::string online;
    std::getline(on, online);
    for (int c : parse_cpu_list(online))
        if (CPU_ISSET(c, &mask)) out.insert(c);
    return out;
}

}  // namespace yac
