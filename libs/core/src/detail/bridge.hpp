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
    static int epollFd(Thread* thread);
    static void registerTimerFd(Thread* thread, int fd,
                                std::shared_ptr<detail::TimerCore> core);
    static void unregisterTimerFd(Thread* thread, int fd);
    static void registerNotifierFd(Thread* thread, int fd,
                                   std::shared_ptr<detail::NotifierCore> core);
    static void unregisterNotifierFd(Thread* thread, int fd);
    /// Wakes the event loop; thread-safe.
    static void wake(Thread* thread);
};

} // namespace Firmir
