#pragma once

// Internal shared state of Firmir::SocketNotifier. Not installed.
// A shared core lets tasks marshalled to the owner thread outlive the
// notifier safely: every marshalled operation holds a shared_ptr and
// re-checks alive.

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace Firmir {

class Thread;
class SocketNotifier;

namespace detail {

struct NotifierCore {
    std::mutex m;           ///< Guards alive, armed, events, notifier, dupFd.
    bool alive = true;      ///< Cleared during SocketNotifier destruction.
    bool armed = false;     ///< True while the descriptor is watched by the loop.
    int fd = -1;            ///< Watched descriptor/socket (not owned).
    int dupFd = -1;         ///< Linux: dup() of fd when fd was already registered.
    /// Windows: WSAEVENT bound to fd via WSAEventSelect (owned).
    std::intptr_t eventHandle = 0;
    std::uint32_t events = 0; ///< Linux: epoll mask; Windows: FD_* mask.
    std::atomic<bool> enabled{false};
    Thread* owner = nullptr;
    SocketNotifier* notifier = nullptr; ///< Valid while alive; loop thread only.
};

/// Registers the descriptor with the owner thread's epoll. Must run in the
/// owner thread's loop (or before the loop starts).
void armNotifier(const std::shared_ptr<NotifierCore>& core);
/// Unregisters the descriptor. Must run in the owner thread's loop,
/// or in any thread when the loop is not active.
void disarmNotifier(const std::shared_ptr<NotifierCore>& core);
/// Same as disarmNotifier but with core->m already locked by the caller.
void disarmNotifierLocked(const std::shared_ptr<NotifierCore>& core);

} // namespace detail
} // namespace Firmir
