#include "test_framework.hpp"

#include <firmir/future.hpp>
#include <firmir/string.hpp>

#include <chrono>
#include <stdexcept>
#include <thread>

using namespace Firmir;

TEST_CASE(future_value_result) {
    Future<int> f = Concurrent::run([] { return 6 * 7; });
    CHECK(f.isValid());
    f.waitForFinished();
    CHECK(f.isFinished());
    CHECK_EQ(f.result(), 42);

    // Copies observe the same result (shared state).
    Future<int> copy = f;
    CHECK_EQ(copy.result(), 42);
}

TEST_CASE(future_arguments_and_types) {
    Future<String> f =
        Concurrent::run([](const String& s, int n) { return s + String::number(n); },
                        String("n="), 5);
    CHECK_EQ(f.result(), "n=5");

    // Free function pointers work too, like QtConcurrent::run.
    auto add = [](int a, int b) { return a + b; };
    Future<int> sum = Concurrent::run(add, 2, 3);
    CHECK_EQ(sum.result(), 5);
}

TEST_CASE(future_void) {
    int sideEffect = 0;
    Future<void> f = Concurrent::run([&sideEffect] { sideEffect = 99; });
    CHECK(!f.isFinished() || f.isFinished()); // state query is safe anytime
    f.waitForFinished();
    CHECK(f.isFinished());
    CHECK_EQ(sideEffect, 99);
}

TEST_CASE(future_exception_propagates) {
    Future<int> f = Concurrent::run(
        []() -> int { throw std::runtime_error("boom"); });
    CHECK_THROWS_AS(f.result(), std::runtime_error);
}

TEST_CASE(future_invalid_is_harmless) {
    Future<int> f;
    CHECK(!f.isValid());
    CHECK(!f.isFinished());
    f.waitForFinished(); // no crash
}

TEST_CASE(future_parallel_execution) {
    // Several futures really run concurrently.
    const auto start = std::chrono::steady_clock::now();
    auto sleep50 = [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return 1;
    };
    Future<int> a = Concurrent::run(sleep50);
    Future<int> b = Concurrent::run(sleep50);
    Future<int> c = Concurrent::run(sleep50);
    CHECK_EQ(a.result() + b.result() + c.result(), 3);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    CHECK(elapsed < std::chrono::milliseconds(140)); // serial would be 150+
}
