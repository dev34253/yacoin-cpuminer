// yacoin-cpuminer: a minimal test framework (one binary per test file,
// registered with ctest). MIT licence.
//
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
//   int main() { return yac_test::run_all(); }   // or use TEST_MAIN()
#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace yac_test {

struct Case {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry()
{
    static std::vector<Case> r;
    return r;
}

inline int& failures()
{
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, std::move(fn)}); }
};

inline void fail(const char* file, int line, const std::string& msg)
{
    ++failures();
    std::cerr << file << ":" << line << ": FAILED: " << msg << "\n";
}

inline int run_all()
{
    int failed_cases = 0;
    for (auto& c : registry()) {
        int before = failures();
        try {
            c.fn();
        } catch (const std::exception& e) {
            fail(__FILE__, __LINE__, std::string("uncaught exception in ") + c.name + ": " + e.what());
        } catch (...) {
            fail(__FILE__, __LINE__, std::string("uncaught non-std exception in ") + c.name);
        }
        bool ok = failures() == before;
        if (!ok) ++failed_cases;
        std::cout << (ok ? "[  OK  ] " : "[ FAIL ] ") << c.name << "\n";
    }
    std::cout << registry().size() - failed_cases << "/" << registry().size() << " test cases passed\n";
    return failed_cases == 0 ? 0 : 1;
}

}  // namespace yac_test

#define YAC_CAT2(a, b) a##b
#define YAC_CAT(a, b) YAC_CAT2(a, b)
#define TEST(name)                                                                   \
    static void YAC_CAT(test_fn_, name)();                                           \
    static yac_test::Registrar YAC_CAT(test_reg_, name)(#name, YAC_CAT(test_fn_, name)); \
    static void YAC_CAT(test_fn_, name)()

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) yac_test::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        auto&& yac_a_ = (a);                                                               \
        auto&& yac_b_ = (b);                                                               \
        if (!(yac_a_ == yac_b_)) {                                                         \
            std::ostringstream yac_os_;                                                    \
            yac_os_ << "CHECK_EQ(" #a ", " #b ")\n    left:  " << yac_a_ << "\n    right: " << yac_b_; \
            yac_test::fail(__FILE__, __LINE__, yac_os_.str());                             \
        }                                                                                  \
    } while (0)

#define CHECK_THROWS(expr)                                                         \
    do {                                                                           \
        bool yac_threw_ = false;                                                   \
        try {                                                                      \
            (void)(expr);                                                          \
        } catch (...) {                                                            \
            yac_threw_ = true;                                                     \
        }                                                                          \
        if (!yac_threw_) yac_test::fail(__FILE__, __LINE__, "CHECK_THROWS(" #expr ")"); \
    } while (0)

#define TEST_MAIN() \
    int main() { return yac_test::run_all(); }
