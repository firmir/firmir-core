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

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#else
#include <fcntl.h>
#include <sys/epoll.h>
#include <unistd.h>
#endif

namespace Firmir {

namespace {

// Event masks stored in NotifierCore::events: EPOLL* on Linux, FD_* on
// Windows (WSAEventSelect).
#ifdef _WIN32
constexpr std::uint32_t kEventRead = FD_READ;
constexpr std::uint32_t kEventWrite = FD_WRITE;
constexpr std::uint32_t kEventExcept = FD_OOB;

/// WSAStartup, once per process. SocketNotifier is Winsock's only user in
/// firmir-core; the networking module initializes it on its own too.
bool ensureWsa() {
    static const bool ok = [] {
        WSADATA data;
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}
#else
constexpr std::uint32_t kEventRead = EPOLLIN;
constexpr std::uint32_t kEventWrite = EPOLLOUT;
constexpr std::uint32_t kEventExcept = EPOLLPRI;
#endif

} // namespace

namespace detail {

void armNotifier(const std::shared_ptr<NotifierCore>& core) {
    Thread* owner = core->owner;
    std::lock_guard<std::mutex> lk(core->m);
    if (!core->alive)
        return;
#ifdef _WIN32
    if (!ensureWsa())
        throw std::system_error(::WSAGetLastError(), std::system_category(),
                                "WSAStartup");
    const SOCKET socket = static_cast<SOCKET>(
        static_cast<std::uintptr_t>(core->fd));
    if (!core->armed) {
        // Winsock allows only ONE WSAEventSelect binding per socket: a second
        // call would silently replace the first notifier's event. Notifiers
        // watching the same socket therefore share one manual-reset event,
        // and the masks are ORed together. WSAEnumNetworkEvents() both
        // reports and clears the events, so no dup() workaround is needed.
        std::uint32_t siblingMask = 0;
        WSAEVENT ev = reinterpret_cast<WSAEVENT>(
            ThreadBridgeAccess::notifierEventForSocket(owner, core->fd,
                                                       &siblingMask));
        const bool shared = ev != nullptr;
        if (!shared) {
            ev = ::WSACreateEvent();
            if (ev == WSA_INVALID_EVENT)
                throw std::system_error(::WSAGetLastError(),
                                        std::system_category(),
                                        "WSACreateEvent");
        }
        if (::WSAEventSelect(socket, ev, core->events | siblingMask) ==
            SOCKET_ERROR) {
            const int err = ::WSAGetLastError();
            if (!shared)
                ::WSACloseEvent(ev);
            throw std::system_error(err, std::system_category(),
                                    "WSAEventSelect");
        }
        core->eventHandle = reinterpret_cast<std::intptr_t>(ev);
        ThreadBridgeAccess::registerNotifierFd(owner, core->eventHandle, core);
    } else {
        // Re-arm: merge with the masks of the siblings sharing the event.
        std::uint32_t siblingMask = 0;
        ThreadBridgeAccess::notifierEventForSocket(owner, core->fd,
                                                   &siblingMask);
        if (::WSAEventSelect(socket,
                             reinterpret_cast<WSAEVENT>(core->eventHandle),
                             core->events | siblingMask) == SOCKET_ERROR)
            throw std::system_error(::WSAGetLastError(), std::system_category(),
                                    "WSAEventSelect");
    }
    core->armed = true;
    return;
#else
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
#endif
}

void disarmNotifierLocked(const std::shared_ptr<NotifierCore>& core) {
    // Caller holds core->m and runs in the owner thread (or the loop is
    // inactive).
    if (core->armed) {
#ifdef _WIN32
        if (core->eventHandle) {
            const WSAEVENT ev = reinterpret_cast<WSAEVENT>(core->eventHandle);
            const SOCKET socket = static_cast<SOCKET>(
                static_cast<std::uintptr_t>(core->fd));
            const std::intptr_t key = core->eventHandle;
            ThreadBridgeAccess::unregisterNotifierFd(core->owner, key, core);
            // Siblings may still watch the same socket through this event:
            // shrink the mask instead of closing it in that case.
            std::uint32_t siblingMask = 0;
            const bool shared = ThreadBridgeAccess::notifierEventForSocket(
                core->owner, core->fd, &siblingMask) != 0;
            ::WSAEventSelect(socket, shared ? ev : nullptr, siblingMask);
            if (!shared)
                ::WSACloseEvent(ev);
            core->eventHandle = 0;
        }
#else
        const int epfd = ThreadBridgeAccess::epollFd(core->owner);
        const int watched = core->dupFd >= 0 ? core->dupFd : core->fd;
        ::epoll_ctl(epfd, EPOLL_CTL_DEL, watched, nullptr);
        ThreadBridgeAccess::unregisterNotifierFd(core->owner, watched, core);
        if (core->dupFd >= 0) {
            ::close(core->dupFd);
            core->dupFd = -1;
        }
#endif
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
        d->core->events = kEventRead;
        break;
    case Type::Write:
        d->core->events = kEventWrite;
        break;
    case Type::Exception:
        d->core->events = kEventExcept;
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
    case kEventRead:
        return Type::Read;
    case kEventWrite:
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

void Thread::Impl::processNotifierEvent(std::intptr_t fd) {
    auto it = notifiers.find(fd);
    if (it == notifiers.end())
        return;

#ifdef _WIN32
    const std::vector<std::shared_ptr<detail::NotifierCore>> cores = it->second;
    if (cores.empty())
        return;
    // Reports the fired events and resets the manual-reset WSAEVENT. All
    // cores in this entry watch the same socket (Winsock allows only one
    // WSAEventSelect binding per socket), so filter each by its own mask.
    WSANETWORKEVENTS fired{};
    if (::WSAEnumNetworkEvents(
            static_cast<SOCKET>(static_cast<std::uintptr_t>(cores.front()->fd)),
            reinterpret_cast<WSAEVENT>(fd), &fired) == SOCKET_ERROR)
        return;
    for (const auto& core : cores) {
        if (!(fired.lNetworkEvents & core->events))
            continue;
        SocketNotifier* notifier = nullptr;
        int watchedFd = -1;
        {
            std::lock_guard<std::mutex> lk(core->m);
            if (!core->alive || !core->armed)
                continue;
            notifier = core->notifier;
            watchedFd = core->fd;
        }
        if (notifier)
            notifier->emitSignal("activated", watchedFd);
    }
#else
    const std::shared_ptr<detail::NotifierCore> core = it->second;
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
#endif
}

} // namespace Firmir
