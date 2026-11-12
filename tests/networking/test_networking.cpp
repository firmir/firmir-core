#include "test_framework.hpp"

#include <firmir/networkaccessmanager.hpp>
#include <firmir/networkreply.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
using socklen_t = int;
#else
#  include <netdb.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

using namespace Firmir;

namespace {

#ifdef _WIN32
using TestSocket = SOCKET;
constexpr TestSocket kInvalidSocket = INVALID_SOCKET;
constexpr int kNoSignalFlag = 0; // MSG_NOSIGNAL does not exist on Windows
void closeSocket(TestSocket s) { ::closesocket(s); }
#else
using TestSocket = int;
constexpr TestSocket kInvalidSocket = -1;
constexpr int kNoSignalFlag = MSG_NOSIGNAL;
void closeSocket(TestSocket s) { ::close(s); }
#endif

int sendAll(TestSocket s, const char* data, std::size_t size) {
    return ::send(s, data, static_cast<int>(size), kNoSignalFlag);
}

/// A one-shot blocking HTTP server on 127.0.0.1 with an ephemeral port.
class TestHttpServer {
public:
    TestHttpServer() {
#ifdef _WIN32
        WSADATA wsa{};
        ::WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&one),
                     static_cast<socklen_t>(sizeof(one)));
        struct sockaddr_in addr {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(listenFd_, reinterpret_cast<struct sockaddr*>(&addr),
               static_cast<socklen_t>(sizeof(addr)));
        ::listen(listenFd_, 4);
        socklen_t len = sizeof(addr);
        ::getsockname(listenFd_, reinterpret_cast<struct sockaddr*>(&addr),
                      &len);
        port_ = ntohs(addr.sin_port);
    }

    ~TestHttpServer() {
        if (listenFd_ != kInvalidSocket)
            closeSocket(listenFd_);
    }

    int port() const { return port_; }

    /// Serves `response` to the next connection, then closes it.
    void serveOnce(const std::string& response) {
        struct sockaddr_in peer {};
        socklen_t len = sizeof(peer);
        const TestSocket conn = ::accept(
            listenFd_, reinterpret_cast<struct sockaddr*>(&peer), &len);
        if (conn == kInvalidSocket)
            return;
        // Drain the request headers.
        std::string request;
        char buf[1024];
        for (;;) {
            const int n = ::recv(conn, buf, static_cast<int>(sizeof(buf)), 0);
            if (n <= 0)
                break;
            request.append(buf, static_cast<std::size_t>(n));
            if (request.find("\r\n\r\n") != std::string::npos)
                break;
        }
        lastRequest_ = request;
        sendAll(conn, response.data(), response.size());
        closeSocket(conn);
    }

    std::string lastRequest() const { return lastRequest_; }

    /// Raw acceptor for tests that need a custom (e.g. silent) connection.
    TestSocket listenFdForTest() const { return listenFd_; }

private:
    TestSocket listenFd_ = kInvalidSocket;
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
#ifdef _WIN32
    WSADATA wsa{};
    ::WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    // Bind and close a socket to obtain a guaranteed-free port.
    TestSocket fd = ::socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ::bind(fd, reinterpret_cast<struct sockaddr*>(&addr),
           static_cast<socklen_t>(sizeof(addr)));
    socklen_t len = sizeof(addr);
    ::getsockname(fd, reinterpret_cast<struct sockaddr*>(&addr), &len);
    const int port = ntohs(addr.sin_port);
    closeSocket(fd);

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

TEST_CASE(networking_transfer_timeout) {
    // The server accepts the connection but never answers: the request
    // must finish with Error::Timeout instead of hanging forever.
    TestHttpServer server;
    std::thread silentThread([&server] {
        struct sockaddr_in peer {};
        socklen_t len = sizeof(peer);
        const TestSocket conn =
            ::accept(server.listenFdForTest(),
                     reinterpret_cast<struct sockaddr*>(&peer), &len);
        if (conn == kInvalidSocket)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        closeSocket(conn);
    });

    NetworkAccessManager manager;
    NetworkRequest request("http://127.0.0.1:" + String::number(server.port()) +
                           "/silent");
    request.setTransferTimeout(std::chrono::milliseconds(400));
    const auto started = std::chrono::steady_clock::now();
    auto* reply = manager.get(request);

    CHECK(waitFinished(reply, 5000)); // the 3 s silence would fail without a timeout
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(elapsed < std::chrono::milliseconds(2500));
    CHECK_EQ(reply->error(), NetworkReply::Error::Timeout);
    silentThread.join();
}

TEST_CASE(networking_connect_timeout) {
    // 192.0.2.0/24 (TEST-NET-1, RFC 5737) is unroutable: the connect either
    // times out via our deadline or fails immediately — never hangs.
    NetworkAccessManager manager;
    NetworkRequest request("http://192.0.2.1:81/nowhere");
    request.setConnectTimeout(std::chrono::milliseconds(300));
    request.setTransferTimeout(std::chrono::milliseconds(2000));
    const auto started = std::chrono::steady_clock::now();
    auto* reply = manager.get(request);

    CHECK(waitFinished(reply, 6000));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(elapsed < std::chrono::milliseconds(5000));
    CHECK(reply->error() != NetworkReply::Error::NoError);
}

TEST_CASE(networking_current_thread_timeout) {
    // Same silent-server scenario through the SocketNotifier path: the
    // watchdog timer must finish the reply with Error::Timeout.
    TestHttpServer server;
    std::thread silentThread([&server] {
        struct sockaddr_in peer {};
        socklen_t len = sizeof(peer);
        const TestSocket conn =
            ::accept(server.listenFdForTest(),
                     reinterpret_cast<struct sockaddr*>(&peer), &len);
        if (conn == kInvalidSocket)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        closeSocket(conn);
    });

    NetworkAccessManager manager;
    manager.setRequestMode(NetworkAccessManager::RequestMode::CurrentThread);
    NetworkRequest request("http://127.0.0.1:" + String::number(server.port()) +
                           "/silent");
    request.setTransferTimeout(std::chrono::milliseconds(400));
    auto* reply = manager.get(request);
    CHECK(!reply->isFinished());

    Object context;
    bool finished = false;
    reply->connect(
        &context, NetworkReply::signalFinished(),
        [&] {
            finished = true;
            CHECK(reply->isFinished());
            CHECK_EQ(reply->error(), NetworkReply::Error::Timeout);
            Thread::mainThread()->quit();
        },
        ConnectionMode::Direct);
    Timer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(std::chrono::milliseconds(2500));
    watchdog.connect(&context, "timeout",
                     [] { Thread::mainThread()->exit(9); },
                     ConnectionMode::Direct);
    watchdog.start();
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    watchdog.stop();
    silentThread.join();
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
