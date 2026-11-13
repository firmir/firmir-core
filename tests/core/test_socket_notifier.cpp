#include "test_framework.hpp"

#include <firmir/object.hpp>
#include <firmir/socketnotifier.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <cerrno>
#include <chrono>
#include <memory>
#include <stdexcept>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/epoll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

using namespace Firmir;
using namespace std::chrono_literals;

namespace {

#ifdef _WIN32
using TestSocket = SOCKET;
constexpr TestSocket kInvalidSocket = INVALID_SOCKET;
void closeSocket(TestSocket s) { ::closesocket(s); }
#else
using TestSocket = int;
constexpr TestSocket kInvalidSocket = -1;
void closeSocket(TestSocket s) { ::close(s); }
#endif

struct SocketPair {
    TestSocket fds[2] = {kInvalidSocket, kInvalidSocket};
    SocketPair() {
#ifdef _WIN32
        // No AF_UNIX socketpair() on Windows: emulate with loopback TCP.
        WSADATA wsa{};
        CHECK_EQ(::WSAStartup(MAKEWORD(2, 2), &wsa), 0);
        TestSocket listener = ::socket(AF_INET, SOCK_STREAM, 0);
        CHECK(listener != kInvalidSocket);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        CHECK_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&addr),
                        static_cast<int>(sizeof(addr))),
                 0);
        CHECK_EQ(::listen(listener, 1), 0);
        int len = static_cast<int>(sizeof(addr));
        CHECK_EQ(::getsockname(listener, reinterpret_cast<sockaddr*>(&addr),
                               &len),
                 0);
        // A blocking connect to a loopback listener completes via the backlog.
        fds[1] = ::socket(AF_INET, SOCK_STREAM, 0);
        CHECK(fds[1] != kInvalidSocket);
        CHECK_EQ(::connect(fds[1], reinterpret_cast<const sockaddr*>(&addr),
                           static_cast<int>(sizeof(addr))),
                 0);
        fds[0] = ::accept(listener, nullptr, nullptr);
        ::closesocket(listener);
        CHECK(fds[0] != kInvalidSocket);
#else
        const int rc = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        CHECK_EQ(rc, 0);
#endif
    }
    ~SocketPair() {
        if (fds[0] != kInvalidSocket)
            closeSocket(fds[0]);
        if (fds[1] != kInvalidSocket)
            closeSocket(fds[1]);
    }
};

/// SocketNotifier takes an int; SOCKET is pointer-sized on Windows.
int notifierFd(TestSocket s) { return static_cast<int>(s); }

int recvBytes(TestSocket s, char* buf, std::size_t size) {
    return ::recv(s, buf, static_cast<int>(size), 0);
}

int sendBytes(TestSocket s, const char* buf, std::size_t size) {
    return ::send(s, buf, static_cast<int>(size), 0);
}

/// Sends the single urgent byte MSG_OOB requires; 1 on success, -1 with errno
/// set when the platform or the socket type rejects out-of-band data.
int sendOob(TestSocket s) {
    char byte = 'z';
    return ::send(s, &byte, 1, MSG_OOB);
}

#ifndef _WIN32
/// Connected loopback TCP pair. MSG_OOB is not accepted by every socket type on
/// every system: AF_UNIX socketpairs reject it on some kernels and under the
/// qemu-user emulation used by distribution build services, while TCP supports
/// it wherever urgent data works at all.
struct TcpPair {
    TestSocket fds[2] = {kInvalidSocket, kInvalidSocket};
    TcpPair() {
        const TestSocket listener = ::socket(AF_INET, SOCK_STREAM, 0);
        CHECK(listener != kInvalidSocket);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        CHECK_EQ(::bind(listener, reinterpret_cast<sockaddr*>(&addr),
                        static_cast<socklen_t>(sizeof(addr))),
                 0);
        CHECK_EQ(::listen(listener, 1), 0);
        socklen_t len = static_cast<socklen_t>(sizeof(addr));
        CHECK_EQ(::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len), 0);
        fds[1] = ::socket(AF_INET, SOCK_STREAM, 0);
        CHECK(fds[1] != kInvalidSocket);
        CHECK_EQ(::connect(fds[1], reinterpret_cast<const sockaddr*>(&addr),
                           static_cast<socklen_t>(sizeof(addr))),
                 0);
        fds[0] = ::accept(listener, nullptr, nullptr);
        ::close(listener);
        CHECK(fds[0] != kInvalidSocket);
    }
    ~TcpPair() {
        if (fds[0] != kInvalidSocket)
            closeSocket(fds[0]);
        if (fds[1] != kInvalidSocket)
            closeSocket(fds[1]);
    }
};

/// Queues an urgent byte on @p source and asks a bare epoll whether @p target
/// reports it as EPOLLPRI. Separates environment limitations (qemu-user,
/// restricted kernels that accept MSG_OOB but never signal it) from a bug in
/// the notifier, which is what the exception test is meant to catch.
bool urgentDataReportedByPoll(TestSocket target, TestSocket source) {
    if (sendOob(source) != 1)
        return false;
    const int epfd = ::epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0)
        return false;
    epoll_event registration{};
    registration.events = EPOLLPRI;
    bool reported = false;
    if (::epoll_ctl(epfd, EPOLL_CTL_ADD, target, &registration) == 0) {
        epoll_event events[4];
        const int count = ::epoll_wait(epfd, events, 4, 1000);
        reported = count > 0 && (events[0].events & EPOLLPRI) != 0;
    }
    ::close(epfd);
    return reported;
}
#endif

// Runs the main-thread event loop until it is quit; aborts via watchdog.
int runLoop(std::chrono::milliseconds watchdogTimeout = 3000ms) {
    Object ctx;
    Timer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(watchdogTimeout);
    watchdog.connect(&ctx, "timeout",
                     [] { Thread::mainThread()->exit(1); },
                     ConnectionMode::Direct);
    watchdog.start();
    return Thread::mainThread()->exec();
}

} // namespace

TEST_CASE(socket_notifier_read) {
    SocketPair pair;
    SocketNotifier notifier(notifierFd(pair.fds[0]), SocketNotifier::Type::Read);
    CHECK(notifier.isEnabled());
    CHECK(notifier.type() == SocketNotifier::Type::Read);
    CHECK_EQ(notifier.socket(), notifierFd(pair.fds[0]));

    Object ctx;
    bool fired = false;
    notifier.connect<int>(&ctx, "activated",
                          [&](int fd) {
                              fired = true;
                              char buf[8];
                              const int n = recvBytes(fd, buf, sizeof(buf));
                              CHECK_EQ(n, 1);
                              Thread::mainThread()->quit();
                          },
                          ConnectionMode::Direct);

    CHECK_EQ(sendBytes(pair.fds[1], "x", 1), 1);
    CHECK_EQ(runLoop(), 0);
    CHECK(fired);
}

TEST_CASE(socket_notifier_disable) {
    SocketPair pair;
    SocketNotifier notifier(notifierFd(pair.fds[0]), SocketNotifier::Type::Read);
    notifier.setEnabled(false);
    CHECK(!notifier.isEnabled());

    Object ctx;
    bool fired = false;
    notifier.connect<int>(&ctx, "activated", [&](int) { fired = true; });

    CHECK_EQ(sendBytes(pair.fds[1], "x", 1), 1);
    // Nothing should fire; quit via a short single-shot timer.
    Timer::singleShot(50ms, &ctx, [] { Thread::mainThread()->quit(); });
    CHECK_EQ(runLoop(), 0);
    CHECK(!fired);

    // Re-enable: the pending byte fires the notifier.
    notifier.setEnabled(true);
    notifier.connect<int>(&ctx, "activated",
                          [&](int fd) {
                              fired = true;
                              char buf[8];
                              const int n = recvBytes(fd, buf, sizeof(buf));
                              CHECK_EQ(n, 1);
                              Thread::mainThread()->quit();
                          },
                          ConnectionMode::Direct);
    CHECK_EQ(runLoop(), 0);
    CHECK(fired);
}

TEST_CASE(socket_notifier_write) {
    SocketPair pair;
    // An empty socketpair is writable right away; a level-triggered Write
    // notifier would fire forever, so disable it in the first activation.
    SocketNotifier notifier(notifierFd(pair.fds[0]),
                            SocketNotifier::Type::Write);

    Object ctx;
    int count = 0;
    notifier.connect<int>(&ctx, "activated",
                          [&](int) {
                              ++count;
                              notifier.setEnabled(false);
                              Thread::mainThread()->quit();
                          },
                          ConnectionMode::Direct);

    CHECK_EQ(runLoop(), 0);
    CHECK_EQ(count, 1);
    CHECK(!notifier.isEnabled());
}

TEST_CASE(socket_notifier_exception_oob) {
    // Type::Exception is EPOLLPRI, which only fires for out-of-band data.
    // MSG_OOB depends on the socket type and on the environment, so probe the
    // transports first and fall back to loopback TCP instead of failing the
    // build where urgent data is simply unavailable.
    SocketPair pair;
    TestSocket target = pair.fds[0];
    TestSocket source = pair.fds[1];
    const char* transport = "AF_UNIX socketpair";
    bool oobSupported;
    {
        SocketPair probe;
        oobSupported = sendOob(probe.fds[1]) == 1;
    }
#ifndef _WIN32
    std::unique_ptr<TcpPair> tcpPair;
    if (!oobSupported) {
        TcpPair probe;
        oobSupported = sendOob(probe.fds[1]) == 1;
        if (oobSupported) {
            tcpPair = std::make_unique<TcpPair>();
            target = tcpPair->fds[0];
            source = tcpPair->fds[1];
            transport = "loopback TCP";
        }
    }
#endif
    if (!oobSupported) {
        std::fprintf(stderr,
                     "[ SKIP ] MSG_OOB is not supported on this platform, "
                     "exception notifier not checked\n");
        return;
    }

    SocketNotifier notifier(notifierFd(target), SocketNotifier::Type::Exception);

    Object ctx;
    bool fired = false;
    notifier.connect<int>(&ctx, "activated",
                          [&](int fd) {
                              fired = true;
                              char buf[8];
                              const int n = ::recv(
                                  fd, buf, static_cast<int>(sizeof(buf)),
                                  MSG_OOB);
                              CHECK_EQ(n, 1);
                              Thread::mainThread()->quit();
                          },
                          ConnectionMode::Direct);

    if (sendOob(source) != 1) {
        std::fprintf(stderr,
                     "[ SKIP ] MSG_OOB send failed on %s (errno %d), "
                     "exception notifier not checked\n",
                     transport, errno);
        return;
    }
    // Does the kernel signal urgent data at all here? When it does, a notifier
    // that stays silent is a real failure; when it does not (qemu-user,
    // restricted build kernels), the check is skipped instead of failing.
    bool urgentDataReported = true;
#ifndef _WIN32
    if (tcpPair) {
        TcpPair probe;
        urgentDataReported = urgentDataReportedByPoll(probe.fds[0], probe.fds[1]);
    } else {
        SocketPair probe;
        urgentDataReported = urgentDataReportedByPoll(probe.fds[0], probe.fds[1]);
    }
#endif
    const int loopResult = runLoop();
    if (loopResult != 0 || !fired) {
        if (!urgentDataReported) {
            std::fprintf(stderr,
                         "[ SKIP ] %s does not report urgent data as an event in "
                         "this environment, exception notifier not checked\n",
                         transport);
            return;
        }
        CHECK_EQ(loopResult, 0);
        CHECK(fired);
    }
}

TEST_CASE(socket_notifier_two_on_same_fd) {
    SocketPair pair;
    // epoll cannot register one fd twice; the second notifier transparently
    // works through a dup(). Verify both fire.
    SocketNotifier reader(notifierFd(pair.fds[0]), SocketNotifier::Type::Read);
    SocketNotifier writer(notifierFd(pair.fds[0]), SocketNotifier::Type::Write);

    Object ctx;
    bool readFired = false;
    bool writeFired = false;
    writer.connect<int>(&ctx, "activated",
                        [&](int) {
                            writeFired = true;
                            writer.setEnabled(false);
                            if (readFired)
                                Thread::mainThread()->quit();
                        },
                        ConnectionMode::Direct);
    reader.connect<int>(&ctx, "activated",
                        [&](int fd) {
                            readFired = true;
                            char buf[8];
                            const int n = recvBytes(fd, buf, sizeof(buf));
                            CHECK_EQ(n, 1);
                            if (writeFired)
                                Thread::mainThread()->quit();
                        },
                        ConnectionMode::Direct);

    CHECK_EQ(sendBytes(pair.fds[1], "x", 1), 1);
    CHECK_EQ(runLoop(), 0);
    CHECK(readFired);
    CHECK(writeFired);
}

TEST_CASE(socket_notifier_move_enabled_vetoed) {
    Thread thread;
    thread.start();
    SocketPair pair;
    auto* notifier =
        new SocketNotifier(notifierFd(pair.fds[0]), SocketNotifier::Type::Read);
    CHECK_THROWS_AS(notifier->moveToThread(&thread), std::logic_error);

    notifier->setEnabled(false);
    notifier->moveToThread(&thread); // Disabled: the move is allowed.
    notifier->deleteLater();         // Destroyed inside the worker thread.
    thread.quit();
    thread.wait();
}
