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
    bool armed = false;     ///< True while fd is registered in epoll.
    int fd = -1;            ///< Watched descriptor (not owned).
    int dupFd = -1;         ///< dup() of fd when fd was already registered.
    std::uint32_t events = 0; ///< epoll event mask.
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
