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
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Firmir {

namespace detail {
struct TimerCore;
struct NotifierCore;
}

class Thread::Impl {
public:
    Impl(Thread* owner, bool mainThread);
    ~Impl();

    /// Enqueues a task (FIFO) and wakes the loop (eventfd / wake event).
    void postTask(std::function<void()> task);
    /// Wakes up the event loop wait.
    void wake();
    /// Runs the event loop in the calling thread; returns the exit code.
    int execLoop();
    /// Pops and executes all pending tasks in FIFO order.
    void drainTasks();
    /// Handles a fired timer (key: Linux timerfd, Windows timer HANDLE).
    void processTimerEvent(std::intptr_t fd);
    /// Handles an event on a descriptor watched by a SocketNotifier
    /// (key: Linux fd, Windows WSAEVENT).
    void processNotifierEvent(std::intptr_t fd);
    /// Deletes adopted (singleShot) timers that never fired. Called when the
    /// loop exits and from ~Impl; requires that no loop is concurrently active.
    void cleanupAdoptedTimers();

    std::thread::id executionId() const;
    void setExecutionId(std::thread::id id);
    std::thread::id workerId() const;
    void setWorkerId(std::thread::id id);

    Thread* q;
    bool isMain;
#ifdef _WIN32
    HANDLE wakeEvent = nullptr; // auto-reset event, signaled by wake()
#else
    int epollFd = -1;
    int wakeFd = -1;
#endif

    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> finished{true};
    std::atomic<bool> quitRequested{false};
    std::atomic<bool> loopActive{false};
    std::atomic<int> exitCode{0};

    std::mutex taskMutex;
    std::deque<std::function<void()>> tasks;

    // Owned by the loop thread only: timer key -> timer core
    // (Linux: timerfd; Windows: waitable timer HANDLE).
    std::unordered_map<std::intptr_t, std::shared_ptr<detail::TimerCore>> timers;
    // Owned by the loop thread only: watched key -> notifier core(s)
    // (Linux: fd; Windows: WSAEVENT). Windows: cores watching the same socket
    // share one entry — Winsock allows only one WSAEventSelect per socket.
#ifdef _WIN32
    std::unordered_map<std::intptr_t,
                       std::vector<std::shared_ptr<detail::NotifierCore>>>
        notifiers;
#else
    std::unordered_map<std::intptr_t, std::shared_ptr<detail::NotifierCore>> notifiers;
#endif

    mutable std::mutex idMutex;
    std::thread::id creatorId;
    std::thread::id execId;
    std::thread::id workerThreadId;

    std::mutex waitMutex;
    std::condition_variable waitCv;
};

} // namespace Firmir
