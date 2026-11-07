#include <firmir/timer.hpp>

#include <firmir/thread.hpp>

#include "detail/bridge.hpp"
#include "detail/timer_core.hpp"

#include <cerrno>
#include <cstdint>
#include <future>
#include <stdexcept>
#include <system_error>
#include <thread>

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

namespace Firmir {

namespace detail {

void armTimer(const std::shared_ptr<TimerCore>& core) {
    Thread* owner = core->owner;
    std::lock_guard<std::mutex> lk(core->m);
    if (!core->alive)
        return;
    const std::int64_t ms = core->intervalMs.load();
    if (ms <= 0)
        return;

    if (core->fd < 0) {
        core->fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (core->fd < 0)
            throw std::system_error(errno, std::generic_category(), "timerfd_create");
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = core->fd;
        const int epfd = ThreadBridgeAccess::epollFd(owner);
        if (::epoll_ctl(epfd, EPOLL_CTL_ADD, core->fd, &ev) < 0) {
            const int err = errno;
            ::close(core->fd);
            core->fd = -1;
            throw std::system_error(err, std::generic_category(), "epoll_ctl(timerfd)");
        }
        ThreadBridgeAccess::registerTimerFd(owner, core->fd, core);
    }

    itimerspec spec{};
    spec.it_value.tv_sec = ms / 1000;
    spec.it_value.tv_nsec = (ms % 1000) * 1000000L;
    if (!core->single.load()) {
        spec.it_interval.tv_sec = spec.it_value.tv_sec;
        spec.it_interval.tv_nsec = spec.it_value.tv_nsec;
    }
    if (::timerfd_settime(core->fd, 0, &spec, nullptr) < 0)
        throw std::system_error(errno, std::generic_category(), "timerfd_settime");
    core->armed = true;
    core->active.store(true);
}

void disarmTimerLocked(const std::shared_ptr<TimerCore>& core) {
    // Caller holds core->m and runs in the owner thread (or the loop is inactive).
    if (core->fd >= 0) {
        const int epfd = ThreadBridgeAccess::epollFd(core->owner);
        ::epoll_ctl(epfd, EPOLL_CTL_DEL, core->fd, nullptr);
        ThreadBridgeAccess::unregisterTimerFd(core->owner, core->fd);
        ::close(core->fd);
        core->fd = -1;
    }
    core->armed = false;
    core->active.store(false);
}

void disarmTimer(const std::shared_ptr<TimerCore>& core) {
    std::lock_guard<std::mutex> lk(core->m);
    disarmTimerLocked(core);
}

} // namespace detail

class Timer::Impl {
public:
    std::shared_ptr<detail::TimerCore> core = std::make_shared<detail::TimerCore>();
};

Timer::Timer(Object* parent) : Object(parent), d(std::make_unique<Impl>()) {
    d->core->owner = thread();
    d->core->timer = this;
}

void Timer::onThreadChanging(Thread* target) {
    std::lock_guard<std::mutex> lock(d->core->m);
    if (d->core->armed)
        throw std::logic_error(
            "Firmir::Timer: cannot move an active timer to another thread");
    d->core->owner = target;
}

Timer::~Timer() {
    const std::shared_ptr<detail::TimerCore> core = d->core;
    Thread* owner = core->owner;
    const bool inLoop =
        ThreadBridgeAccess::executionId(owner) == std::this_thread::get_id();
    if (inLoop || !ThreadBridgeAccess::loopActive(owner)) {
        std::lock_guard<std::mutex> lk(core->m);
        core->alive = false;
        core->timer = nullptr;
        detail::disarmTimerLocked(core);
    } else {
        // Foreign thread with a running loop: tear down synchronously inside
        // the owner thread so no timer event can race with destruction.
        std::promise<void> done;
        std::future<void> future = done.get_future();
        ThreadBridgeAccess::postTask(owner, [core, &done] {
            {
                std::lock_guard<std::mutex> lk(core->m);
                core->alive = false;
                core->timer = nullptr;
            }
            detail::disarmTimer(core);
            done.set_value();
        });
        future.wait();
    }
}

void Timer::setInterval(std::chrono::milliseconds interval) {
    if (interval.count() <= 0)
        throw std::invalid_argument(
            "Firmir::Timer::setInterval: interval must be positive");
    d->core->intervalMs.store(interval.count());
    if (isActive())
        start();
}

std::chrono::milliseconds Timer::interval() const {
    return std::chrono::milliseconds(d->core->intervalMs.load());
}

void Timer::setSingleShot(bool singleShot) { d->core->single.store(singleShot); }
bool Timer::isSingleShot() const { return d->core->single.load(); }
bool Timer::isActive() const { return d->core->active.load(); }

void Timer::start() {
    const std::shared_ptr<detail::TimerCore> core = d->core;
    Thread* owner = core->owner;
    if (ThreadBridgeAccess::executionId(owner) == std::this_thread::get_id()) {
        detail::armTimer(core);
    } else {
        ThreadBridgeAccess::postTask(owner, [core] { detail::armTimer(core); });
    }
}

void Timer::start(std::chrono::milliseconds interval) {
    if (interval.count() <= 0)
        throw std::invalid_argument("Firmir::Timer::start: interval must be positive");
    d->core->intervalMs.store(interval.count());
    start();
}

void Timer::stop() {
    const std::shared_ptr<detail::TimerCore> core = d->core;
    core->active.store(false);
    Thread* owner = core->owner;
    if (ThreadBridgeAccess::executionId(owner) == std::this_thread::get_id()) {
        detail::disarmTimer(core);
    } else {
        ThreadBridgeAccess::postTask(owner, [core] { detail::disarmTimer(core); });
    }
}

void Timer::singleShot(std::chrono::milliseconds interval, Object* context,
                       std::function<void()> slot) {
    if (!context)
        throw std::invalid_argument("Firmir::Timer::singleShot: null context");
    auto* timer = new Timer();
    if (timer->thread() != context->thread()) {
        delete timer;
        throw std::logic_error(
            "Firmir::Timer::singleShot: must be called from the context's thread");
    }
    timer->setSingleShot(true);
    timer->setInterval(interval);
    {
        std::lock_guard<std::mutex> lk(timer->d->core->m);
        timer->d->core->adopted = true; // Owner thread deletes it if it never fires.
    }
    timer->connect(context, "timeout",
                   [timer, slot = std::move(slot)]() mutable {
                       slot();
                       timer->deleteLater();
                   },
                   ConnectionMode::Direct);
    timer->start();
}

} // namespace Firmir

