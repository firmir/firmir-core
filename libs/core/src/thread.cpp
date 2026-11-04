#include <firmir/thread.hpp>

#include <firmir/timer.hpp>

#include "detail/bridge.hpp"
#include "detail/notifier_core.hpp"
#include "detail/thread_impl.hpp"
#include "detail/timer_core.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace Firmir {

namespace {
thread_local Thread* t_currentThread = nullptr;
}

Thread::Impl::Impl(Thread* owner, bool mainThread)
    : q(owner), isMain(mainThread), creatorId(std::this_thread::get_id()),
      execId(creatorId) {
    epollFd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epollFd < 0)
        throw std::system_error(errno, std::generic_category(), "epoll_create1");
    wakeFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (wakeFd < 0) {
        const int err = errno;
        ::close(epollFd);
        throw std::system_error(err, std::generic_category(), "eventfd");
    }
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = wakeFd;
    if (::epoll_ctl(epollFd, EPOLL_CTL_ADD, wakeFd, &ev) < 0) {
        const int err = errno;
        ::close(wakeFd);
        ::close(epollFd);
        throw std::system_error(err, std::generic_category(), "epoll_ctl(eventfd)");
    }
}

Thread::Impl::~Impl() {
    cleanupAdoptedTimers();
    if (wakeFd >= 0)
        ::close(wakeFd);
    if (epollFd >= 0)
        ::close(epollFd);
}

void Thread::Impl::postTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lk(taskMutex);
        tasks.push_back(std::move(task));
    }
    wake();
}

void Thread::Impl::wake() {
    const std::uint64_t one = 1;
    const ssize_t n = ::write(wakeFd, &one, sizeof(one));
    (void)n; // EAGAIN only means the counter is saturated: the loop wakes anyway.
}

void Thread::Impl::drainTasks() {
    std::deque<std::function<void()>> local;
    {
        std::lock_guard<std::mutex> lk(taskMutex);
        local.swap(tasks);
    }
    for (auto& task : local)
        task();
}

std::thread::id Thread::Impl::executionId() const {
    std::lock_guard<std::mutex> lk(idMutex);
    return execId;
}

void Thread::Impl::setExecutionId(std::thread::id id) {
    std::lock_guard<std::mutex> lk(idMutex);
    execId = id;
}

std::thread::id Thread::Impl::workerId() const {
    std::lock_guard<std::mutex> lk(idMutex);
    return workerThreadId;
}

void Thread::Impl::setWorkerId(std::thread::id id) {
    std::lock_guard<std::mutex> lk(idMutex);
    workerThreadId = id;
}

void Thread::Impl::processTimerEvent(int fd) {
    std::uint64_t expirations = 0;
    const ssize_t n = ::read(fd, &expirations, sizeof(expirations));
    (void)n;

    std::shared_ptr<detail::TimerCore> core;
    auto it = timers.find(fd);
    if (it == timers.end())
        return;
    core = it->second;

    Timer* timer = nullptr;
    {
        std::lock_guard<std::mutex> lk(core->m);
        if (!core->armed)
            return;
        timer = core->timer;
        if (core->single.load())
            detail::disarmTimerLocked(core);
    }
    if (timer)
        timer->emitSignal("timeout");
}

void Thread::Impl::cleanupAdoptedTimers() {
    std::vector<Timer*> adoptedTimers;
    for (auto& [fd, core] : timers) {
        std::lock_guard<std::mutex> lk(core->m);
        if (core->alive && core->adopted && core->timer)
            adoptedTimers.push_back(core->timer);
    }
    for (Timer* timer : adoptedTimers)
        delete timer; // Timer's destructor disarms and unregisters itself.
}

int Thread::Impl::execLoop() {
    bool expected = false;
    if (!loopActive.compare_exchange_strong(expected, true))
        throw std::logic_error("Firmir::Thread::exec: event loop already running");

    // Do not clear quitRequested here: a quit() that arrived before the
    // loop started must still take effect (the flag is reset on exit).
    Thread* previous = t_currentThread;
    t_currentThread = q;
    setExecutionId(std::this_thread::get_id());

    // Drop stale wake-up credits accumulated while the loop was not running.
    std::uint64_t stale = 0;
    while (::read(wakeFd, &stale, sizeof(stale)) > 0) {
    }
    drainTasks(); // Tasks may have been posted before the loop started.

    while (!quitRequested.load(std::memory_order_relaxed)) {
        epoll_event events[16];
        const int count = ::epoll_wait(epollFd, events, 16, -1);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        for (int i = 0; i < count; ++i) {
            const int fd = events[i].data.fd;
            if (fd == wakeFd) {
                std::uint64_t value = 0;
                while (::read(wakeFd, &value, sizeof(value)) > 0) {
                }
                drainTasks();
            } else if (timers.find(fd) != timers.end()) {
                processTimerEvent(fd);
            } else {
                processNotifierEvent(fd);
            }
        }
    }

    drainTasks(); // Deliver tasks posted just before/after quit (deleteLater etc.).
    cleanupAdoptedTimers();
    const int code = exitCode.load();
    // Reset for a potential future exec() call.
    quitRequested.store(false);
    exitCode.store(0);
    setExecutionId(creatorId);
    t_currentThread = previous;
    loopActive.store(false);
    return code;
}


Thread::Thread(Object* parent) : Object(parent, nullptr), d(std::make_unique<Impl>(this, false)) {}

struct Thread::MainTag {};

Thread::Thread(MainTag) : Object(nullptr, static_cast<Thread*>(this)),
                          d(std::make_unique<Impl>(this, true)) {}

Thread::~Thread() {
    if (d->worker.joinable()) {
        if (!d->finished.load())
            quit();
        d->worker.join();
    }
    // Our Impl member is destroyed before the Object base subobject; detach
    // the self-affinity so ~Object never dereferences the dead Impl.
    clearAffinity();
}

void Thread::start() {
    if (d->isMain)
        throw std::logic_error("Firmir::Thread::start: the main thread cannot be started");
    if (d->worker.joinable()) {
        if (!d->finished.load())
            throw std::logic_error("Firmir::Thread::start: thread already running");
        d->worker.join();
    }
    d->finished.store(false);
    d->running.store(true);
    d->quitRequested.store(false);
    d->worker = std::thread([this] {
        d->setWorkerId(std::this_thread::get_id());
        setup();
        run();
        cleanup();
        d->running.store(false);
        {
            std::lock_guard<std::mutex> lk(d->waitMutex);
            d->finished.store(true);
        }
        d->waitCv.notify_all();
    });
}

void Thread::quit() { exit(0); }

void Thread::exit(int code) {
    d->exitCode.store(code);
    d->quitRequested.store(true);
    d->wake();
}

bool Thread::wait() {
    if (d->worker.joinable())
        d->worker.join();
    return true;
}

bool Thread::wait(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lk(d->waitMutex);
    const bool done = d->waitCv.wait_for(lk, timeout, [&] { return d->finished.load(); });
    lk.unlock();
    if (done && d->worker.joinable())
        d->worker.join();
    return done;
}

bool Thread::isRunning() const { return d->running.load(); }
bool Thread::isFinished() const { return d->finished.load(); }

int Thread::exec() {
    if (!d->isMain && std::this_thread::get_id() != d->workerId())
        throw std::logic_error(
            "Firmir::Thread::exec: must be called from the thread's own worker");
    return d->execLoop();
}

void Thread::setup() {}
void Thread::run() { exec(); }
void Thread::cleanup() {}

Thread* Thread::currentThread() { return t_currentThread; }

Thread* Thread::mainThread() {
    static Thread mainThreadInstance(MainTag{});
    static const std::thread::id mainThreadId = std::this_thread::get_id();
    if (t_currentThread == nullptr && std::this_thread::get_id() == mainThreadId)
        t_currentThread = &mainThreadInstance;
    return &mainThreadInstance;
}


// ---- Internal bridge -------------------------------------------------------

void ThreadBridgeAccess::postTask(Thread* thread, std::function<void()> task) {
    thread->d->postTask(std::move(task));
}

std::thread::id ThreadBridgeAccess::executionId(Thread* thread) {
    return thread->d->executionId();
}

bool ThreadBridgeAccess::loopActive(Thread* thread) {
    return thread->d->loopActive.load();
}

int ThreadBridgeAccess::epollFd(Thread* thread) {
    return thread->d->epollFd;
}

void ThreadBridgeAccess::registerTimerFd(Thread* thread, int fd,
                                         std::shared_ptr<detail::TimerCore> core) {
    thread->d->timers.emplace(fd, std::move(core));
}

void ThreadBridgeAccess::unregisterTimerFd(Thread* thread, int fd) {
    thread->d->timers.erase(fd);
}

void ThreadBridgeAccess::registerNotifierFd(
    Thread* thread, int fd, std::shared_ptr<detail::NotifierCore> core) {
    thread->d->notifiers.emplace(fd, std::move(core));
}

void ThreadBridgeAccess::unregisterNotifierFd(Thread* thread, int fd) {
    thread->d->notifiers.erase(fd);
}

void ThreadBridgeAccess::wake(Thread* thread) { thread->d->wake(); }

} // namespace Firmir

