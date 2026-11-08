#include "test_framework.hpp"

#include <firmir/networkaccessmanager.hpp>
#include <firmir/networkreply.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <chrono>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

using namespace Firmir;

namespace {

/// A one-shot blocking HTTP server on 127.0.0.1 with an ephemeral port.
class TestHttpServer {
public:
    TestHttpServer() {
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        struct sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(listenFd_, reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr));
        ::listen(listenFd_, 4);
        socklen_t len = sizeof(addr);
        ::getsockname(listenFd_, reinterpret_cast<struct sockaddr*>(&addr),
                      &len);
        port_ = ntohs(addr.sin_port);
    }

    ~TestHttpServer() {
        if (listenFd_ != -1)
            ::close(listenFd_);
    }

    int port() const { return port_; }

    /// Serves `response` to the next connection, then closes it.
    void serveOnce(const std::string& response) {
        struct sockaddr_in peer {};
        socklen_t len = sizeof(peer);
        const int conn = ::accept(
            listenFd_, reinterpret_cast<struct sockaddr*>(&peer), &len);
        if (conn == -1)
            return;
        // Drain the request headers.
        std::string request;
        char buf[1024];
        for (;;) {
            const ssize_t n = ::recv(conn, buf, sizeof(buf), 0);
            if (n <= 0)
                break;
            request.append(buf, static_cast<std::size_t>(n));
            if (request.find("\r\n\r\n") != std::string::npos)
                break;
        }
        lastRequest_ = request;
        ::send(conn, response.data(), response.size(), MSG_NOSIGNAL);
        ::close(conn);
    }

    std::string lastRequest() const { return lastRequest_; }

private:
    int listenFd_ = -1;
    int port_ = 0;
    std::string lastRequest_;
};

/// Waits until the reply is finished (with a timeout).
bool waitFinished(const NetworkReply* reply, int timeoutMs = 5000) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (!reply->isFinished()) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

} // namespace

TEST_CASE(networking_get) {
    TestHttpServer server;
    const std::string body = "hello firmir";
    const std::string response = "HTTP/1.1 200 OK\r\n"
                                 "Content-Type: text/plain\r\n"
                                 "X-Test: yes\r\n"
                                 "\r\n" +
                                 body;

    NetworkAccessManager manager;
    auto* reply = manager.get(NetworkRequest(
        "http://127.0.0.1:" + String::number(server.port()) + "/path"));

    std::thread serverThread(
        [&server, &response]() { server.serveOnce(response); });

    CHECK(waitFinished(reply));
    serverThread.join();

    CHECK(reply->error() == NetworkReply::Error::NoError);
    CHECK_EQ(reply->httpStatus(), 200);
    CHECK_EQ(reply->readAll().toStdString(), body);
    CHECK_EQ(reply->header("content-type"), "text/plain");
    CHECK_EQ(reply->header("x-test"), "yes");
    CHECK(server.lastRequest().find("GET /path HTTP/1.1") == 0);
}

TEST_CASE(networking_post) {
    TestHttpServer server;
    const std::string response = "HTTP/1.1 201 Created\r\n\r\n";

    NetworkRequest request("http://127.0.0.1:" +
                           String::number(server.port()) + "/submit");
    request.setHeader("Content-Type", "text/plain");
    request.setBody(ByteArray("payload"));

    NetworkAccessManager manager;
    auto* reply = manager.post(request);

    std::thread serverThread(
        [&server, &response]() { server.serveOnce(response); });

    CHECK(waitFinished(reply));
    serverThread.join();

    CHECK(reply->error() == NetworkReply::Error::NoError);
    CHECK_EQ(reply->httpStatus(), 201);
    CHECK(server.lastRequest().find("POST /submit HTTP/1.1") == 0);
    CHECK(server.lastRequest().find("payload") != std::string::npos);
}

TEST_CASE(networking_http_error_status) {
    TestHttpServer server;
    const std::string response = "HTTP/1.1 404 Not Found\r\n\r\n";

    NetworkAccessManager manager;
    auto* reply = manager.get(NetworkRequest(
        "http://127.0.0.1:" + String::number(server.port()) + "/missing"));

    std::thread serverThread(
        [&server, &response]() { server.serveOnce(response); });

    CHECK(waitFinished(reply));
    serverThread.join();

    CHECK(reply->error() == NetworkReply::Error::HttpError);
    CHECK_EQ(reply->httpStatus(), 404);
}

TEST_CASE(networking_connection_refused) {
    // Bind and close a socket to obtain a guaranteed-free port.
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    const int port = ntohs(addr.sin_port);
    ::close(fd);

    NetworkAccessManager manager;
    auto* reply = manager.get(
        NetworkRequest("http://127.0.0.1:" + String::number(port) + "/"));

    CHECK(waitFinished(reply));
    CHECK(reply->error() == NetworkReply::Error::NetworkError);
    CHECK(!reply->errorString().isEmpty());
}

TEST_CASE(networking_invalid_url) {
    NetworkAccessManager manager;
    auto* reply = manager.get(NetworkRequest("not-a-url"));
    CHECK(waitFinished(reply));
    CHECK(reply->error() == NetworkReply::Error::InvalidUrl);

    auto* reply2 = manager.get(NetworkRequest("ftp://example.com/x"));
    CHECK(waitFinished(reply2));
    CHECK(reply2->error() == NetworkReply::Error::InvalidUrl);
}

TEST_CASE(networking_request_headers) {
    NetworkRequest request("http://example.com/");
    CHECK_EQ(request.url(), "http://example.com/");
    request.setUrl("http://example.com/other");
    CHECK_EQ(request.url(), "http://example.com/other");

    request.setHeader("Accept", "application/json");
    request.setHeader("Accept", "text/plain"); // replace
    CHECK_EQ(request.header("Accept"), "text/plain");
    CHECK(request.header("missing").isEmpty());

    NetworkRequest copy = request;
    CHECK_EQ(copy.url(), request.url());
    CHECK_EQ(copy.header("Accept"), "text/plain");
}

TEST_CASE(networking_worker_finished_signal) {
    TestHttpServer server;
    const std::string response = "HTTP/1.1 200 OK\r\n\r\nworker";
    NetworkAccessManager manager;
    CHECK(manager.requestMode() ==
          NetworkAccessManager::RequestMode::WorkerThread);
    auto* reply = manager.get(NetworkRequest(
        "http://127.0.0.1:" + String::number(server.port()) + "/worker"));

    Object context;
    bool finished = false;
    reply->connect(
        &context, NetworkReply::signalFinished(),
        [&] {
            finished = true;
            CHECK(reply->isFinished());
            CHECK_EQ(reply->readAll().toStdString(), "worker");
            Thread::mainThread()->quit();
        },
        ConnectionMode::Queued);
    std::thread serverThread(
        [&server, &response]() { server.serveOnce(response); });
    Timer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(std::chrono::milliseconds(3000));
    watchdog.connect(&context, "timeout",
                     [] { Thread::mainThread()->exit(9); },
                     ConnectionMode::Direct);
    watchdog.start();
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    watchdog.stop();
    serverThread.join();
    CHECK(finished);
}

TEST_CASE(networking_current_thread_socket_notifier) {
    TestHttpServer server;
    const std::string body(9000, 'n');
    const std::string response = "HTTP/1.1 200 OK\r\n"
                                 "Content-Type: text/plain\r\n\r\n" + body;
    NetworkAccessManager manager;
    manager.setRequestMode(NetworkAccessManager::RequestMode::CurrentThread);
    CHECK(manager.requestMode() ==
          NetworkAccessManager::RequestMode::CurrentThread);
    auto* reply = manager.get(NetworkRequest(
        "http://127.0.0.1:" + String::number(server.port()) + "/notifier"));

    Object context;
    bool finished = false;
    const auto managerThread = std::this_thread::get_id();
    reply->connect(
        &context, NetworkReply::signalFinished(),
        [&] {
            finished = true;
            CHECK_EQ(std::this_thread::get_id(), managerThread);
            CHECK(reply->isFinished());
            CHECK(reply->error() == NetworkReply::Error::NoError);
            CHECK_EQ(reply->httpStatus(), 200);
            CHECK_EQ(reply->header("content-type"), "text/plain");
            CHECK_EQ(reply->readAll().toStdString(), body);
            Thread::mainThread()->quit();
        },
        ConnectionMode::Direct);
    CHECK(!reply->isFinished());
    std::thread serverThread(
        [&server, &response]() { server.serveOnce(response); });
    Timer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(std::chrono::milliseconds(3000));
    watchdog.connect(&context, "timeout",
                     [] { Thread::mainThread()->exit(9); },
                     ConnectionMode::Direct);
    watchdog.start();
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    watchdog.stop();
    serverThread.join();
    CHECK(finished);
}

TEST_CASE(networking_current_thread_invalid_url) {
    NetworkAccessManager manager;
    manager.setRequestMode(NetworkAccessManager::RequestMode::CurrentThread);
    auto* reply = manager.get(NetworkRequest("not-a-url"));
    CHECK(!reply->isFinished());

    Object context;
    bool finished = false;
    reply->connect(
        &context, NetworkReply::signalFinished(),
        [&] {
            finished = true;
            CHECK(reply->isFinished());
            CHECK(reply->error() == NetworkReply::Error::InvalidUrl);
            Thread::mainThread()->quit();
        },
        ConnectionMode::Direct);
    Timer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(std::chrono::milliseconds(1000));
    watchdog.connect(&context, "timeout",
                     [] { Thread::mainThread()->exit(9); },
                     ConnectionMode::Direct);
    watchdog.start();
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    watchdog.stop();
    CHECK(finished);
}
