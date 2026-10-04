// yacoin-cpuminer: tests for small utilities. MIT licence.
#include "testing.h"

#include <map>
#include <set>
#include <stdexcept>
#include <vector>

#include "app/options.h"
#include "util/sysinfo.h"

#include "hash/yac_scrypt.h"
#include "util/hex.h"

using namespace yac;

TEST(hex_round_trip)
{
    std::vector<uint8_t> v = {0x00, 0x01, 0xab, 0xff};
    CHECK_EQ(to_hex(v), std::string("0001abff"));
    CHECK(from_hex("0001ABff") == v);
    CHECK_EQ(to_hex_reversed(v.data(), v.size()), std::string("ffab0100"));
}

TEST(hex_rejects_bad_input)
{
    CHECK_THROWS(from_hex("abc"));
    CHECK_THROWS(from_hex("zz"));
}

TEST(scrypt_jane_self_test_passes)
{
    CHECK(yac_scrypt_self_test() == 1);
    CHECK(std::string(yac_scrypt_mix_name()).find("ChaCha") != std::string::npos);
}

TEST(cpu_topology_orders)
{
    // This laptop's layout: siblings (0,4) (1,5) (2,6) (3,7), not adjacent.
    std::map<int, std::string> sib;
    const char* lists[] = {"0,4", "1,5", "2,6", "3,7", "0,4", "1,5", "2,6", "3,7"};
    for (int c = 0; c < 8; ++c) sib[c] = lists[c];
    auto cores = core_groups(sib);
    CHECK_EQ(cores.size(), size_t(4));
    std::vector<int> compact = cpu_order(cores, false), spread = cpu_order(cores, true);
    CHECK(compact == (std::vector<int>{0, 4, 1, 5, 2, 6, 3, 7}));
    CHECK(spread == (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7}));
    // Range syntax and a CPU without SMT.
    std::map<int, std::string> sib2 = {{0, "0-1"}, {1, "0-1"}, {2, "2"}};
    CHECK(cpu_order(core_groups(sib2), true) == (std::vector<int>{0, 2, 1}));
    CHECK(parse_cpu_list("0-2,8") == (std::vector<int>{0, 1, 2, 8}));
    // 6 threads (the live setting): compact leaves core 3 idle, spread uses all cores.
    CHECK(std::vector<int>(compact.begin(), compact.begin() + 6) == (std::vector<int>{0, 4, 1, 5, 2, 6}));
    CHECK(std::vector<int>(spread.begin(), spread.begin() + 6) == (std::vector<int>{0, 1, 2, 3, 4, 5}));
}

TEST(cpu_topology_filters_and_parser_edges)
{
    std::map<int, std::string> sib;
    const char* lists[] = {"0,4", "1,5", "2,6", "3,7", "0,4", "1,5", "2,6", "3,7"};
    for (int c = 0; c < 8; ++c) sib[c] = lists[c];
    // Only usable CPUs (taskset -c 0-5): cores 2 and 3 keep one thread each.
    auto cores = core_groups(sib, std::set<int>{0, 1, 2, 3, 4, 5});
    CHECK(cpu_order(cores, false) == (std::vector<int>{0, 4, 1, 5, 2, 3}));
    CHECK(cpu_order(cores, true) == (std::vector<int>{0, 1, 2, 3, 4, 5}));
    // Inconsistent sibling lists never place a CPU twice.
    std::map<int, std::string> odd = {{0, "0,4"}, {4, "4"}};
    CHECK(cpu_order(core_groups(odd), true) == (std::vector<int>{0, 4}));
    CHECK(cpu_order(core_groups({}), true).empty());
    CHECK(parse_cpu_list("").empty());
    CHECK(parse_cpu_list("3-").empty());
    CHECK(parse_cpu_list("5-3").empty());
    CHECK(parse_cpu_list("3x,1").size() == 1);
    CHECK(parse_cpu_list("0-2000000000").empty());
    CHECK(parse_cpu_list("2\n") == (std::vector<int>{2}));
}

TEST(affinity_option_parses)
{
    const char* ok[] = {"miner", "--affinity", "spread"};
    CHECK(parse_options(3, const_cast<char**>(ok), false).affinity == "spread");
    const char* def[] = {"miner"};
    CHECK(parse_options(1, const_cast<char**>(def), false).affinity == "none");
    const char* bad[] = {"miner", "--affinity", "random"};
    bool threw = false;
    try {
        parse_options(3, const_cast<char**>(bad), false);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}

TEST_MAIN()
