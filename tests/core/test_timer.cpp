#include "test_framework.hpp"

#include <firmir/log.hpp>
#include <firmir/object.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

using namespace Firmir;
using namespace std::chrono_literals;

TEST_CASE(timer_single_shot) {
    Object ctx;
    Timer timer;
    timer.setSingleShot(true);
    timer.setInterval(40ms);
    CHECK(!timer.isActive());
    CHECK_EQ(timer.interval().count(), 40);

    int fired = 0;
    timer.connect(&ctx, "timeout",
                  [&] {
                      ++fired;
                      Thread::mainThread()->quit();
                  },
                  ConnectionMode::Direct);

    const auto started = std::chrono::steady_clock::now();
    timer.start();
    CHECK(timer.isActive());
    Timer::singleShot(2s, &ctx, [] { Thread::mainThread()->exit(9); });
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    CHECK_EQ(fired, 1);
    CHECK(!timer.isActive()); // single-shot stops automatically
    CHECK(elapsed >= 30ms);
}

TEST_CASE(timer_periodic_and_object_timers) {
    class Clock : public Object {
    public:
        void timerEvent(int id) override {
            CHECK_EQ(id, timerId);
            ++ticks;
            if (ticks == 3) {
                killTimer(id);
                Thread::mainThread()->quit();
            }
        }
        int timerId = -1;
        int ticks = 0;
    } clock;

    clock.timerId = clock.startTimer(20ms);
    Timer::singleShot(2s, &clock, [] { Thread::mainThread()->exit(9); });
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    CHECK_EQ(clock.ticks, 3);
}

TEST_CASE(timer_stop_from_foreign_thread) {
    struct TrackedTimer : Timer {
        explicit TrackedTimer(std::atomic<bool>* f) : flag(f) {}
        ~TrackedTimer() override { flag->store(true); }
        std::atomic<bool>* flag;
    };
    std::atomic<bool> destroyed{false};

    Thread worker;
    worker.start();

    auto* timer = new TrackedTimer(&destroyed);
    timer->moveToThread(&worker);
    timer->setInterval(10ms);

    std::atomic<int> count{0};
    timer->connect(timer, "timeout", [&] { count.fetch_add(1); },
                   ConnectionMode::Direct);

    timer->start(); // marshalled into the worker thread
    CHECK(testing::waitFor([&] { return count.load() >= 5; }));
    CHECK(timer->isActive());

    timer->stop(); // marshalled as well
    CHECK(testing::waitFor([&] { return !timer->isActive(); }));
    const int frozen = count.load();
    std::this_thread::sleep_for(100ms);
    CHECK_EQ(count.load(), frozen);

    timer->deleteLater();
    CHECK(testing::waitFor([&] { return destroyed.load(); }));
    worker.quit();
    CHECK(worker.wait(5s));
}

TEST_CASE(timer_singleshot_static) {
    Object ctx;
    bool fired = false;
    Timer::singleShot(30ms, &ctx, [&] {
        fired = true;
        Thread::mainThread()->quit();
    });
    Timer::singleShot(2s, &ctx, [] { Thread::mainThread()->exit(9); });
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    CHECK(fired);
}

TEST_CASE(timer_invalid_interval) {
    Timer timer;
    CHECK_THROWS_AS(timer.setInterval(0ms), std::invalid_argument);
    CHECK_THROWS_AS(timer.start(-5ms), std::invalid_argument);
}

TEST_CASE(thread_eventloop_trace_reports_slow_handler) {
    // FIRMIR_EVENTLOOP_TRACE=1 names the handler that blocked the event loop: a
    // stalled loop delays every frame request of the thread, and a frame trace
    // alone only shows the resulting gap, not what caused it.
    testing::setEnv("FIRMIR_EVENTLOOP_TRACE", "1");

    std::mutex messageMutex;
    std::string message;
    Log::setSink([&](LogLevel level, const std::string& category,
                     const std::string& text) {
        if (level != LogLevel::Warning || category != "thread")
            return;
        std::lock_guard<std::mutex> lock(messageMutex);
        message = text;
    });

    Object ctx;
    Timer timer;
    timer.setInterval(10ms);
    int ticks = 0;
    timer.connect(&ctx, "timeout",
                  [&] {
                      if (++ticks == 1)
                          std::this_thread::sleep_for(30ms); // slow handler
                      else
                          Thread::mainThread()->quit();
                  },
                  ConnectionMode::Direct);
    Timer::singleShot(2s, &ctx, [] { Thread::mainThread()->exit(9); });
    timer.start();
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    timer.stop();
    Log::setSink(nullptr);
    testing::setEnv("FIRMIR_EVENTLOOP_TRACE", "");

    std::lock_guard<std::mutex> lock(messageMutex);
    CHECK(message.find("event loop busy") != std::string::npos);
    // The interval identifies which timer it was.
    CHECK(message.find("timer 10 ms") != std::string::npos);
}
