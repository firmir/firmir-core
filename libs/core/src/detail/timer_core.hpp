#pragma once

// Internal shared state of Firmir::Timer. Not installed.
// A shared core lets tasks marshalled to the owner thread outlive the Timer
// safely: every marshalled operation holds a shared_ptr and re-checks alive.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace Firmir {

class Thread;
class Timer;

namespace detail {

struct TimerCore {
    std::mutex m;          ///< Guards alive, armed, fd, timer.
    bool alive = true;     ///< Cleared during Timer destruction.
    bool armed = false;    ///< True while fd is registered in epoll.
    bool adopted = false;  ///< singleShot: owner thread deletes it if it never fires.
    int fd = -1;           ///< timerfd, created/closed in the owner thread.
    std::atomic<bool> active{false};
    std::atomic<bool> single{false};
    std::atomic<std::int64_t> intervalMs{0};
    Thread* owner = nullptr;
    Timer* timer = nullptr; ///< Valid while alive; dereferenced in loop thread only.
};

/// Arms (or rearms) the timerfd. Must run in the owner thread's loop.
void armTimer(const std::shared_ptr<TimerCore>& core);
/// Disarms and closes the timerfd. Must run in the owner thread's loop,
/// or in any thread when the loop is not active.
void disarmTimer(const std::shared_ptr<TimerCore>& core);
/// Same as disarmTimer but with core->m already locked by the caller.
void disarmTimerLocked(const std::shared_ptr<TimerCore>& core);

} // namespace detail
} // namespace Firmir
