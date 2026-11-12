#pragma once

// Minimal self-contained test framework: registry + CHECK macros.
// The process aborts (non-zero exit) at the first failed check.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace testing {

inline std::vector<std::pair<std::string, std::function<void()>>>& registry() {
    static std::vector<std::pair<std::string, std::function<void()>>> tests;
    return tests;
}

inline int runAll() {
    int passed = 0;
    for (auto& [name, fn] : registry()) {
        const char* filter = std::getenv("FIRMIR_TEST_FILTER");
        const char* skipFilter = std::getenv("FIRMIR_TEST_SKIP_FILTER");
        if (filter && *filter && name.find(filter) == std::string::npos)
            continue;
        if (skipFilter && *skipFilter && name.find(skipFilter) != std::string::npos)
            continue;
        std::fprintf(stderr, "[ RUN  ] %s\n", name.c_str());
        fn();
        ++passed;
        std::fprintf(stderr, "[ PASS ] %s\n", name.c_str());
    }
    std::fprintf(stderr, "[ DONE ] %d test(s) passed\n", passed);
    return 0;
}

/// Polls @p pred until true or @p timeout elapses; returns the last value.
template <typename F>
bool waitFor(F&& pred, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}

/// Windows has no setenv/unsetenv; _putenv("name=") deletes a variable.
inline void setEnv(const char* name, const char* value) {
#ifdef _WIN32
    ::_putenv((std::string(name) + "=" + value).c_str());
#else
    ::setenv(name, value, 1);
#endif
}

inline void unsetEnv(const char* name) {
#ifdef _WIN32
    ::_putenv((std::string(name) + "=").c_str());
#else
    ::unsetenv(name);
#endif
}

} // namespace testing

#define TEST_CASE(name)                                                        \
    static void name();                                                        \
    static const bool name##_registered = [] {                                 \
        ::testing::registry().emplace_back(#name, name);                       \
        return true;                                                           \
    }();                                                                       \
    static void name()

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "[ FAIL ] %s:%d: CHECK(%s) failed\n",         \
                         __FILE__, __LINE__, #cond);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

#define CHECK_THROWS_AS(expr, ExType)                                          \
    do {                                                                       \
        bool caught_ = false;                                                  \
        try {                                                                  \
            expr;                                                              \
        } catch (const ExType&) {                                              \
            caught_ = true;                                                    \
        }                                                                      \
        if (!caught_) {                                                        \
            std::fprintf(stderr,                                               \
                         "[ FAIL ] %s:%d: CHECK_THROWS_AS(%s) failed\n",       \
                         __FILE__, __LINE__, #expr);                           \
            std::abort();                                                      \
        }                                                                      \
    } while (0)
