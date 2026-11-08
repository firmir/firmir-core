#include "test_framework.hpp"

#include <firmir/object.hpp>
#include <firmir/socketnotifier.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <chrono>
#include <stdexcept>

#include <sys/socket.h>
#include <unistd.h>

using namespace Firmir;
using namespace std::chrono_literals;

namespace {

struct SocketPair {
    int fds[2] = {-1, -1};
    SocketPair() {
        const int rc = ::socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
        CHECK_EQ(rc, 0);
    }
    ~SocketPair() {
        if (fds[0] >= 0)
            ::close(fds[0]);
        if (fds[1] >= 0)
            ::close(fds[1]);
    }
};

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
    SocketNotifier notifier(pair.fds[0], SocketNotifier::Type::Read);
    CHECK(notifier.isEnabled());
    CHECK(notifier.type() == SocketNotifier::Type::Read);
    CHECK_EQ(notifier.socket(), pair.fds[0]);

    Object ctx;
    bool fired = false;
    notifier.connect<int>(&ctx, "activated",
                          [&](int fd) {
                              fired = true;
                              char buf[8];
                              const ssize_t n = ::read(fd, buf, sizeof(buf));
                              CHECK_EQ(n, 1);
                              Thread::mainThread()->quit();
                          },
                          ConnectionMode::Direct);

    CHECK_EQ(::write(pair.fds[1], "x", 1), 1);
    CHECK_EQ(runLoop(), 0);
    CHECK(fired);
}

TEST_CASE(socket_notifier_disable) {
    SocketPair pair;
    SocketNotifier notifier(pair.fds[0], SocketNotifier::Type::Read);
    notifier.setEnabled(false);
    CHECK(!notifier.isEnabled());

    Object ctx;
    bool fired = false;
    notifier.connect<int>(&ctx, "activated", [&](int) { fired = true; });

    CHECK_EQ(::write(pair.fds[1], "x", 1), 1);
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
                              const ssize_t n = ::read(fd, buf, sizeof(buf));
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
    SocketNotifier notifier(pair.fds[0], SocketNotifier::Type::Write);

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
    SocketNotifier notifier(pair.fds[0], SocketNotifier::Type::Exception);

    Object ctx;
    bool fired = false;
    notifier.connect<int>(&ctx, "activated",
                          [&](int fd) {
                              fired = true;
                              char buf[8];
                              const ssize_t n =
                                  ::recv(fd, buf, sizeof(buf), MSG_OOB);
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
    SocketNotifier reader(pair.fds[0], SocketNotifier::Type::Read);
    SocketNotifier writer(pair.fds[0], SocketNotifier::Type::Write);

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
                            const ssize_t n = ::read(fd, buf, sizeof(buf));
                            CHECK_EQ(n, 1);
                            if (writeFired)
                                Thread::mainThread()->quit();
                        },
                        ConnectionMode::Direct);

    CHECK_EQ(::write(pair.fds[1], "x", 1), 1);
    CHECK_EQ(runLoop(), 0);
    CHECK(readFired);
    CHECK(writeFired);
}

TEST_CASE(socket_notifier_move_enabled_vetoed) {
    Thread thread;
    thread.start();
    SocketPair pair;
    auto* notifier = new SocketNotifier(pair.fds[0], SocketNotifier::Type::Read);
    CHECK_THROWS_AS(notifier->moveToThread(&thread), std::logic_error);

    notifier->setEnabled(false);
    notifier->moveToThread(&thread); // Disabled: the move is allowed.
    notifier->deleteLater();         // Destroyed inside the worker thread.
    thread.quit();
    thread.wait();
}
