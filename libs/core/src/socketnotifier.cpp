#include <firmir/socketnotifier.hpp>

#include <firmir/thread.hpp>

#include "detail/bridge.hpp"
#include "detail/notifier_core.hpp"
#include "detail/thread_impl.hpp"

#include <cerrno>
#include <future>
#include <stdexcept>
#include <system_error>
#include <thread>

#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>

namespace Firmir {

namespace detail {

void armNotifier(const std::shared_ptr<NotifierCore>& core) {
    Thread* owner = core->owner;
    std::lock_guard<std::mutex> lk(core->m);
    if (!core->alive)
        return;
    epoll_event ev{};
    ev.events = core->events;
    ev.data.fd = core->fd;
    const int epfd = ThreadBridgeAccess::epollFd(owner);
    const int op = core->armed ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;
    if (::epoll_ctl(epfd, op, core->fd, &ev) == 0) {
        if (!core->armed)
            ThreadBridgeAccess::registerNotifierFd(owner, core->fd, core);
        core->armed = true;
        return;
    }
    if (!core->armed && errno == EEXIST) {
        // epoll cannot watch the same fd twice in one set (two notifiers on
        // one descriptor): register a dup() referring to the same open file
        // description instead.
        const int dupFd = ::fcntl(core->fd, F_DUPFD_CLOEXEC, 3);
        if (dupFd < 0)
            throw std::system_error(errno, std::generic_category(), "dup");
        ev.data.fd = dupFd;
        if (::epoll_ctl(epfd, EPOLL_CTL_ADD, dupFd, &ev) < 0) {
            const int err = errno;
            ::close(dupFd);
            throw std::system_error(err, std::generic_category(),
                                    "epoll_ctl(notifier)");
        }
        core->dupFd = dupFd;
        ThreadBridgeAccess::registerNotifierFd(owner, dupFd, core);
        core->armed = true;
        return;
    }
    throw std::system_error(errno, std::generic_category(),
                            "epoll_ctl(notifier)");
}

void disarmNotifierLocked(const std::shared_ptr<NotifierCore>& core) {
    // Caller holds core->m and runs in the owner thread (or the loop is
    // inactive).
    if (core->armed) {
        const int epfd = ThreadBridgeAccess::epollFd(core->owner);
        const int watched = core->dupFd >= 0 ? core->dupFd : core->fd;
        ::epoll_ctl(epfd, EPOLL_CTL_DEL, watched, nullptr);
        ThreadBridgeAccess::unregisterNotifierFd(core->owner, watched);
        if (core->dupFd >= 0) {
            ::close(core->dupFd);
            core->dupFd = -1;
        }
        core->armed = false;
    }
}

void disarmNotifier(const std::shared_ptr<NotifierCore>& core) {
    std::lock_guard<std::mutex> lk(core->m);
    disarmNotifierLocked(core);
}

} // namespace detail

// ---------------------------------------------------------------------------
// SocketNotifier
// ---------------------------------------------------------------------------

class SocketNotifier::Impl {
public:
    std::shared_ptr<detail::NotifierCore> core =
        std::make_shared<detail::NotifierCore>();
};

SocketNotifier::SocketNotifier(int socket, Type type, Object* parent)
    : Object(parent), d(std::make_unique<Impl>()) {
    d->core->owner = thread();
    d->core->notifier = this;
    d->core->fd = socket;
    switch (type) {
    case Type::Read:
        d->core->events = EPOLLIN;
        break;
    case Type::Write:
        d->core->events = EPOLLOUT;
        break;
    case Type::Exception:
        d->core->events = EPOLLPRI;
        break;
    }
    d->core->enabled.store(true);
    // Arm right away; the loop picks the registration up on its next pass.
    if (ThreadBridgeAccess::executionId(thread()) ==
        std::this_thread::get_id())
        detail::armNotifier(d->core);
    else
        ThreadBridgeAccess::postTask(
            thread(), [core = d->core] { detail::armNotifier(core); });
}

SocketNotifier::~SocketNotifier() {
    const std::shared_ptr<detail::NotifierCore> core = d->core;
    Thread* owner = core->owner;
    const bool inLoop =
        ThreadBridgeAccess::executionId(owner) == std::this_thread::get_id();
    if (inLoop || !ThreadBridgeAccess::loopActive(owner)) {
        std::lock_guard<std::mutex> lk(core->m);
        core->alive = false;
        core->notifier = nullptr;
        detail::disarmNotifierLocked(core);
    } else {
        // Foreign thread with a running loop: tear down synchronously inside
        // the owner thread so no event can race with destruction.
        std::promise<void> done;
        std::future<void> future = done.get_future();
        ThreadBridgeAccess::postTask(owner, [core, &done] {
            {
                std::lock_guard<std::mutex> lk(core->m);
                core->alive = false;
                core->notifier = nullptr;
            }
            detail::disarmNotifier(core);
            done.set_value();
        });
        future.wait();
    }
}

int SocketNotifier::socket() const { return d->core->fd; }

SocketNotifier::Type SocketNotifier::type() const {
    switch (d->core->events) {
    case EPOLLIN:
        return Type::Read;
    case EPOLLOUT:
        return Type::Write;
    default:
        return Type::Exception;
    }
}

bool SocketNotifier::isEnabled() const { return d->core->enabled.load(); }


void SocketNotifier::setEnabled(bool enabled) {
    const std::shared_ptr<detail::NotifierCore> core = d->core;
    const bool was = core->enabled.exchange(enabled);
    if (was == enabled)
        return;
    Thread* owner = core->owner;
    const bool inLoop =
        ThreadBridgeAccess::executionId(owner) == std::this_thread::get_id();
    if (enabled) {
        if (inLoop)
            detail::armNotifier(core);
        else
            ThreadBridgeAccess::postTask(owner,
                                         [core] { detail::armNotifier(core); });
    } else {
        if (inLoop || !ThreadBridgeAccess::loopActive(owner))
            detail::disarmNotifier(core);
        else
            ThreadBridgeAccess::postTask(
                owner, [core] { detail::disarmNotifier(core); });
    }
}

void SocketNotifier::onThreadChanging(Thread* target) {
    if (isEnabled())
        throw std::logic_error(
            "Firmir::SocketNotifier: cannot move an enabled notifier to "
            "another thread; disable it first");
    d->core->owner = target;
}

// ---------------------------------------------------------------------------
// Event loop dispatch (Thread::Impl member, kept next to the notifier code)
// ---------------------------------------------------------------------------

void Thread::Impl::processNotifierEvent(int fd) {
    std::shared_ptr<detail::NotifierCore> core;
    auto it = notifiers.find(fd);
    if (it == notifiers.end())
        return;
    core = it->second;

    SocketNotifier* notifier = nullptr;
    int watchedFd = -1;
    {
        std::lock_guard<std::mutex> lk(core->m);
        if (!core->alive || !core->armed)
            return;
        notifier = core->notifier;
        watchedFd = core->fd;
    }
    if (notifier)
        notifier->emitSignal("activated", watchedFd);
}

} // namespace Firmir
