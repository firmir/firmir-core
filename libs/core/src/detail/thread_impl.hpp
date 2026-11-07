#pragma once

// Internal implementation of Firmir::Thread (PIMPL). Not installed.

#include <firmir/thread.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Firmir {

namespace detail {
struct TimerCore;
struct NotifierCore;
}

class Thread::Impl {
public:
    Impl(Thread* owner, bool mainThread);
    ~Impl();

    /// Enqueues a task (FIFO) and wakes the loop through the eventfd.
    void postTask(std::function<void()> task);
    /// Wakes up epoll_wait via the eventfd.
    void wake();
    /// Runs the event loop in the calling thread; returns the exit code.
    int execLoop();
    /// Pops and executes all pending tasks in FIFO order.
    void drainTasks();
    /// Handles a readable timerfd.
    void processTimerEvent(int fd);
    /// Handles an event on a descriptor watched by a SocketNotifier.
    void processNotifierEvent(int fd);
    /// Deletes adopted (singleShot) timers that never fired. Called when the
    /// loop exits and from ~Impl; requires that no loop is concurrently active.
    void cleanupAdoptedTimers();

    std::thread::id executionId() const;
    void setExecutionId(std::thread::id id);
    std::thread::id workerId() const;
    void setWorkerId(std::thread::id id);

    Thread* q;
    bool isMain;
    int epollFd = -1;
    int wakeFd = -1;

    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> finished{true};
    std::atomic<bool> quitRequested{false};
    std::atomic<bool> loopActive{false};
    std::atomic<int> exitCode{0};

    std::mutex taskMutex;
    std::deque<std::function<void()>> tasks;

    // Owned by the loop thread only: timerfd -> timer core.
    std::unordered_map<int, std::shared_ptr<detail::TimerCore>> timers;
    // Owned by the loop thread only: watched fd -> notifier core.
    std::unordered_map<int, std::shared_ptr<detail::NotifierCore>> notifiers;

    mutable std::mutex idMutex;
    std::thread::id creatorId;
    std::thread::id execId;
    std::thread::id workerThreadId;

    std::mutex waitMutex;
    std::condition_variable waitCv;
};

} // namespace Firmir
