#include "test_framework.hpp"

#include <firmir/object.hpp>
#include <firmir/socketnotifier.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <chrono>
#include <stdexcept>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
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
    SocketPair pair;
    SocketNotifier notifier(notifierFd(pair.fds[0]),
                            SocketNotifier::Type::Exception);

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

    CHECK_EQ(::send(pair.fds[1], "z", 1, MSG_OOB), 1);
    CHECK_EQ(runLoop(), 0);
    CHECK(fired);
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
