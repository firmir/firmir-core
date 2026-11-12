#pragma once

// Internal bridge: lets sibling translation units reach Thread internals
// without exposing them in public headers.

#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace Firmir {

class Thread;
class Object;

namespace detail {
struct TimerCore;
struct NotifierCore;
}

struct ThreadBridgeAccess {
    static void postTask(Thread* thread, std::function<void()> task);
    static std::thread::id executionId(Thread* thread);
    static bool loopActive(Thread* thread);
    // Linux: the epoll set of the thread's event loop.
    static int epollFd(Thread* thread);
    // Keys: Linux — the timerfd/watched fd; Windows — the timer/event HANDLE.
    static void registerTimerFd(Thread* thread, std::intptr_t fd,
                                std::shared_ptr<detail::TimerCore> core);
    static void unregisterTimerFd(Thread* thread, std::intptr_t fd);
    static void registerNotifierFd(Thread* thread, std::intptr_t fd,
                                   std::shared_ptr<detail::NotifierCore> core);
    static void unregisterNotifierFd(Thread* thread, std::intptr_t fd,
                                     const std::shared_ptr<detail::NotifierCore>& core);
#ifdef _WIN32
    /// Windows: the WSAEVENT already bound to socket @p fd by another
    /// notifier of this thread (0 when none); @p mask receives the OR of the
    /// registered event masks for @p fd (needed because Winsock allows only
    /// one WSAEventSelect binding per socket).
    static std::intptr_t notifierEventForSocket(Thread* thread, std::intptr_t fd,
                                                std::uint32_t* mask);
#endif
    /// Wakes the event loop; thread-safe.
    static void wake(Thread* thread);
};

} // namespace Firmir
