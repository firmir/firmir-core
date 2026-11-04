#pragma once

// Firmir::SocketNotifier — the analogue of QSocketNotifier.
//
// Watches a file descriptor for readability/writability/out-of-band data
// from the event loop of the object's thread (epoll-based) and emits the
// "activated" signal carrying the descriptor:
//
//   auto* notifier = new Firmir::SocketNotifier(fd, SocketNotifier::Type::Read);
//   notifier->connect<int>(context, "activated", [](int fd) { ... });
//
// Like QSocketNotifier, the notifier is enabled on construction and fires
// repeatedly while the condition holds (level-triggered): read the pending
// data or disable the notifier to stop the notifications.
//
// The descriptor is NOT owned: SocketNotifier never closes it. An enabled
// notifier cannot be moved to another thread (onThreadChanging() vetoes it);
// disable it first.

#include <firmir/object.hpp>

#include <memory>

namespace Firmir {

class SocketNotifier : public Object {
public:
    enum class Type { Read, Write, Exception };

    SocketNotifier(int socket, Type type, Object* parent = nullptr);
    ~SocketNotifier() override;

    SocketNotifier(const SocketNotifier&) = delete;
    SocketNotifier& operator=(const SocketNotifier&) = delete;

    /// The watched descriptor.
    int socket() const;
    Type type() const;

    /// True while the notifier is registered with the event loop.
    bool isEnabled() const;
    /// Enables/disables the notification. Thread-safe.
    void setEnabled(bool enabled);

protected:
    /// Vetoes moving an enabled notifier to another thread.
    void onThreadChanging(Thread* target) override;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
