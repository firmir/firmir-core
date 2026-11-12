#include <firmir/networkaccessmanager.hpp>

#include <firmir/log.hpp>
#include <firmir/networkreply.hpp>
#include <firmir/socketnotifier.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <openssl/ssl.h>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Firmir {

namespace {

// ---------------------------------------------------------------------------
// Socket portability: POSIX on Linux, Winsock2 on Windows.
// ---------------------------------------------------------------------------
#ifdef _WIN32
using SocketFd = SOCKET;
constexpr SocketFd kInvalidSocket = INVALID_SOCKET;
using PollFd = WSAPOLLFD;
using SockLen = int;
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

/// WSAStartup, once per process.
bool ensureWsa() {
    static const bool ok = [] {
        WSADATA data;
        return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ok;
}

int socketError() { return ::WSAGetLastError(); }
bool isIntrError(int e) { return e == WSAEINTR; }
bool isWouldBlockError(int e) { return e == WSAEWOULDBLOCK; }
bool isInProgressError(int e) { return e == WSAEWOULDBLOCK; }
void closeSocketFd(SocketFd fd) { ::closesocket(fd); }

bool setNonBlockingFd(SocketFd fd) {
    u_long on = 1;
    return ::ioctlsocket(fd, FIONBIO, &on) == 0;
}

int pollSocketFd(PollFd* pfd, int timeoutMs) {
    return ::WSAPoll(pfd, 1, timeoutMs);
}
#else
using SocketFd = int;
constexpr SocketFd kInvalidSocket = -1;
using PollFd = struct pollfd;
using SockLen = socklen_t;

bool ensureWsa() { return true; }
int socketError() { return errno; }
bool isIntrError(int e) { return e == EINTR; }
bool isWouldBlockError(int e) { return e == EAGAIN || e == EWOULDBLOCK; }
bool isInProgressError(int e) { return e == EINPROGRESS; }
void closeSocketFd(SocketFd fd) { ::close(fd); }

bool setNonBlockingFd(SocketFd fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int pollSocketFd(PollFd* pfd, int timeoutMs) { return ::poll(pfd, 1, timeoutMs); }
#endif

} // namespace

struct NetworkReply::Impl {
    Error error = Error::NoError;
    String errorString;
    int httpStatus = 0;
    ByteArray body;
    std::vector<std::pair<String, String>> headers;
    String url;
    std::atomic<bool> finished{false};
};

struct NetworkRequest::Impl {
    String url;
    std::vector<std::pair<String, String>> headers;
    ByteArray body;
    std::chrono::milliseconds connectTimeout{10000};
    std::chrono::milliseconds transferTimeout{30000};
};

namespace {

const LoggingCategory& lcNetwork() {
    static const LoggingCategory category("firmir.network");
    return category;
}

struct UrlParts {
    std::string scheme;
    std::string host;
    int port = 80;
    std::string path = "/";
};

bool parseUrl(const std::string& url, UrlParts& out) {
    const std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos)
        return false;
    out.scheme = url.substr(0, schemeEnd);
    if (out.scheme != "http" && out.scheme != "https")
        return false;

    const std::size_t pathStart = url.find('/', schemeEnd + 3);
    const std::string authority =
        pathStart == std::string::npos
            ? url.substr(schemeEnd + 3)
            : url.substr(schemeEnd + 3, pathStart - schemeEnd - 3);
    out.path = pathStart == std::string::npos ? "/" : url.substr(pathStart);

    std::string hostport = authority;
    const std::size_t at = hostport.find('@');
    if (at != std::string::npos)
        hostport = hostport.substr(at + 1);

    const std::size_t colon = hostport.rfind(':');
    if (colon != std::string::npos) {
        out.host = hostport.substr(0, colon);
        out.port = std::atoi(hostport.substr(colon + 1).c_str());
        if (out.port <= 0)
            return false;
    } else {
        out.host = hostport;
        out.port = out.scheme == "https" ? 443 : 80;
    }
    return !out.host.empty();
}

/// Result of a socket operation: Timeout is reported separately so the
/// reply can carry NetworkReply::Error::Timeout.
enum class IoStatus { Ok, Failed, TimedOut, HostNotFound };

/// Milliseconds left until @p deadline; -1 when there is no deadline
/// (a zero timeout). Returns 0 once the deadline has passed.
int remainingMs(const std::chrono::steady_clock::time_point& deadline,
                bool hasDeadline) {
    if (!hasDeadline)
        return -1;
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    return left.count() > 0 ? static_cast<int>(left.count()) : 0;
}

/// A tiny TCP socket wrapper. All blocking operations poll with a deadline
/// so a blackholed host cannot hang a request forever.
class Socket {
public:
    ~Socket() { close(); }

    // OpenSSL's SSL_set_fd takes an int even on Windows; socket values fit.
    int fd() const { return static_cast<int>(fd_); }

    /// Connects to host:port; the TCP connect is bounded by @p timeout
    /// (milliseconds; 0 = wait indefinitely).
    IoStatus connectTo(const std::string& host, int port,
                       std::chrono::milliseconds timeout) {
        if (!ensureWsa())
            return IoStatus::Failed;
        struct addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* result = nullptr;
        if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                          &result) != 0)
            return IoStatus::HostNotFound;
        const bool hasDeadline = timeout.count() > 0;
        const auto deadline =
            std::chrono::steady_clock::now() + timeout;
        IoStatus status = IoStatus::Failed;
        for (struct addrinfo* rp = result; rp; rp = rp->ai_next) {
            fd_ = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd_ == kInvalidSocket)
                continue;
            setNonBlocking();
            if (::connect(fd_, rp->ai_addr,
                          static_cast<SockLen>(rp->ai_addrlen)) == 0) {
                status = IoStatus::Ok;
                break;
            }
            if (isInProgressError(socketError())) {
                const int wait = remainingMs(deadline, hasDeadline);
                if (wait != 0 && pollFd(POLLOUT, wait)) {
                    int soError = 0;
                    SockLen len = sizeof(soError);
                    if (::getsockopt(fd_, SOL_SOCKET, SO_ERROR,
                                     reinterpret_cast<char*>(&soError),
                                     &len) == 0 &&
                        soError == 0) {
                        status = IoStatus::Ok;
                        break;
                    }
                    status = IoStatus::Failed;
                } else {
                    // pollFd() returns false on timeout and on error;
                    // the clock tells them apart.
                    status = remainingMs(deadline, hasDeadline) == 0
                                 ? IoStatus::TimedOut
                                 : IoStatus::Failed;
                }
            }
            closeSocketFd(fd_);
            fd_ = kInvalidSocket;
            if (status == IoStatus::TimedOut)
                break;
        }
        ::freeaddrinfo(result);
        return status;
    }

    /// Waits for @p events for up to @p timeoutMs (-1 = forever). Returns
    /// false on error or timeout expiry.
    bool pollFd(short events, int timeoutMs) {
        PollFd pfd {};
        pfd.fd = fd_;
        pfd.events = events;
        for (;;) {
            const int rc = pollSocketFd(&pfd, timeoutMs);
            if (rc > 0)
                return pfd.revents & (events | POLLERR | POLLHUP);
            if (rc == 0)
                return false; // timeout
            if (!isIntrError(socketError()))
                return false;
        }
    }

    IoStatus writeAll(const std::string& data,
                      const std::chrono::steady_clock::time_point& deadline,
                      bool hasDeadline) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            const int n = ::send(fd_, data.data() + sent,
                                 static_cast<int>(data.size() - sent),
                                 MSG_NOSIGNAL);
            if (n > 0) {
                sent += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0 && isIntrError(socketError()))
                continue;
            if (n < 0 && isWouldBlockError(socketError())) {
                const int wait = remainingMs(deadline, hasDeadline);
                if (wait != 0 && pollFd(POLLOUT, wait))
                    continue;
                return remainingMs(deadline, hasDeadline) == 0
                           ? IoStatus::TimedOut
                           : IoStatus::Failed;
            }
            return IoStatus::Failed;
        }
        return IoStatus::Ok;
    }

    /// Reads until EOF; suitable for "Connection: close" responses.
    IoStatus readAll(std::string& out,
                     const std::chrono::steady_clock::time_point& deadline,
                     bool hasDeadline) {
        char buf[8192];
        for (;;) {
            const int n = ::recv(fd_, buf, static_cast<int>(sizeof(buf)), 0);
            if (n > 0) {
                out.append(buf, static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0)
                return IoStatus::Ok;
            if (isIntrError(socketError()))
                continue;
            if (isWouldBlockError(socketError())) {
                const int wait = remainingMs(deadline, hasDeadline);
                if (wait != 0 && pollFd(POLLIN, wait))
                    continue;
                return remainingMs(deadline, hasDeadline) == 0
                           ? IoStatus::TimedOut
                           : IoStatus::Failed;
            }
            return IoStatus::Failed;
        }
    }

    bool setNonBlocking() { return setNonBlockingFd(fd_); }

    /// Drains currently available bytes without waiting for more data.
    bool readAvailable(std::string& out, bool& eof) {
        char buf[8192];
        for (;;) {
            const int n = ::recv(fd_, buf, static_cast<int>(sizeof(buf)), 0);
            if (n > 0) {
                out.append(buf, static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0) {
                eof = true;
                return true;
            }
            if (isIntrError(socketError()))
                continue;
            if (isWouldBlockError(socketError()))
                return true;
            return false;
        }
    }

    void close() {
        if (fd_ != kInvalidSocket) {
            closeSocketFd(fd_);
            fd_ = kInvalidSocket;
        }
    }

private:
    SocketFd fd_ = kInvalidSocket;
};
/// A minimal TLS wrapper around OpenSSL. The underlying socket is
/// non-blocking; every wait is a poll bounded by a deadline.
class TlsSocket {
public:
    ~TlsSocket() { close(); }

    /// TCP connect + TLS handshake, bounded by @p timeout (0 = forever).
    IoStatus connectTo(const std::string& host, int port,
                       std::chrono::milliseconds timeout) {
        const IoStatus tcpStatus = socket_.connectTo(host, port, timeout);
        if (tcpStatus != IoStatus::Ok)
            return tcpStatus;

        static SSL_CTX* ctx = [] {
            SSL_library_init();
            SSL_CTX* c = SSL_CTX_new(TLS_client_method());
            SSL_CTX_set_default_verify_paths(c);
            return c;
        }();
        if (!ctx)
            return IoStatus::Failed;

        ssl_ = SSL_new(ctx);
        if (!ssl_)
            return IoStatus::Failed;
        SSL_set_fd(ssl_, socket_.fd());
        SSL_set_tlsext_host_name(ssl_, host.c_str());

        const bool hasDeadline = timeout.count() > 0;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        for (;;) {
            const int rc = SSL_connect(ssl_);
            if (rc == 1)
                return IoStatus::Ok;
            const int error = SSL_get_error(ssl_, rc);
            short events = 0;
            if (error == SSL_ERROR_WANT_READ)
                events = POLLIN;
            else if (error == SSL_ERROR_WANT_WRITE)
                events = POLLOUT;
            else {
                close();
                return IoStatus::Failed;
            }
            const int wait = remainingMs(deadline, hasDeadline);
            if (wait == 0 || !socket_.pollFd(events, wait)) {
                close();
                return remainingMs(deadline, hasDeadline) == 0
                           ? IoStatus::TimedOut
                           : IoStatus::Failed;
            }
        }
    }

    IoStatus writeAll(const std::string& data,
                      const std::chrono::steady_clock::time_point& deadline,
                      bool hasDeadline) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            const int n = SSL_write(ssl_, data.data() + sent,
                                    static_cast<int>(data.size() - sent));
            if (n > 0) {
                sent += static_cast<std::size_t>(n);
                continue;
            }
            const int error = SSL_get_error(ssl_, n);
            short events = 0;
            if (error == SSL_ERROR_WANT_READ)
                events = POLLIN;
            else if (error == SSL_ERROR_WANT_WRITE)
                events = POLLOUT;
            else
                return IoStatus::Failed;
            const int wait = remainingMs(deadline, hasDeadline);
            if (wait == 0 || !socket_.pollFd(events, wait))
                return remainingMs(deadline, hasDeadline) == 0
                           ? IoStatus::TimedOut
                           : IoStatus::Failed;
        }
        return IoStatus::Ok;
    }

    IoStatus readAll(std::string& out,
                     const std::chrono::steady_clock::time_point& deadline,
                     bool hasDeadline) {
        char buf[8192];
        for (;;) {
            const int n = SSL_read(ssl_, buf, sizeof(buf));
            if (n > 0) {
                out.append(buf, static_cast<std::size_t>(n));
                continue;
            }
            const int error = SSL_get_error(ssl_, n);
            if (error == SSL_ERROR_ZERO_RETURN)
                return IoStatus::Ok;
            short events = 0;
            if (error == SSL_ERROR_WANT_READ)
                events = POLLIN;
            else if (error == SSL_ERROR_WANT_WRITE)
                events = POLLOUT;
            else if (error == SSL_ERROR_SYSCALL &&
                     isWouldBlockError(socketError()))
                events = POLLIN;
            else if (error == SSL_ERROR_SYSCALL && n == 0)
                return IoStatus::Ok; // orderly close without close_notify
            else
                return IoStatus::Failed;
            const int wait = remainingMs(deadline, hasDeadline);
            if (wait == 0 || !socket_.pollFd(events, wait))
                return remainingMs(deadline, hasDeadline) == 0
                           ? IoStatus::TimedOut
                           : IoStatus::Failed;
        }
    }

    int fd() const { return socket_.fd(); }
    bool setNonBlocking() { return socket_.setNonBlocking(); }

    bool readAvailable(std::string& out, bool& eof) {
        char buf[8192];
        for (;;) {
            const int n = SSL_read(ssl_, buf, sizeof(buf));
            if (n > 0) {
                out.append(buf, static_cast<std::size_t>(n));
                continue;
            }
            const int error = SSL_get_error(ssl_, n);
            if (error == SSL_ERROR_WANT_READ)
                return true;
            if (error == SSL_ERROR_ZERO_RETURN ||
                (n == 0 && error == SSL_ERROR_SYSCALL)) {
                eof = true;
                return true;
            }
            if (error == SSL_ERROR_SYSCALL &&
                isWouldBlockError(socketError()))
                return true;
            return false;
        }
    }

    void close() {
        if (ssl_) {
            SSL_shutdown(ssl_);
            SSL_free(ssl_);
            ssl_ = nullptr;
        }
        socket_.close();
    }

private:
    Socket socket_;
    SSL* ssl_ = nullptr;
};

std::string trimCopy(const std::string& s) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    const auto first = std::find_if(s.begin(), s.end(), notSpace);
    const auto last = std::find_if(s.rbegin(), s.rend(), notSpace).base();
    if (first >= last)
        return {};
    return std::string(first, last);
}

} // namespace

struct NetworkAccessManager::CurrentRequest {
    NetworkReply* reply = nullptr;
    std::string raw;
    std::unique_ptr<Socket> socket;
    std::unique_ptr<TlsSocket> tls;
    // Destroy the notifier before closing the descriptor it watches.
    std::unique_ptr<SocketNotifier> notifier;
};

struct NetworkAccessManager::Impl {
    RequestMode mode = RequestMode::WorkerThread;
    std::vector<std::unique_ptr<CurrentRequest>> currentRequests;
};

NetworkAccessManager::NetworkAccessManager(Object* parent)
    : Object(parent), d(std::make_unique<Impl>()) {}

NetworkAccessManager::~NetworkAccessManager() = default;

void NetworkAccessManager::setRequestMode(RequestMode mode) { d->mode = mode; }
NetworkAccessManager::RequestMode NetworkAccessManager::requestMode() const {
    return d->mode;
}

NetworkReply* NetworkAccessManager::get(const NetworkRequest& request) {
    return send("GET", request);
}
NetworkReply* NetworkAccessManager::post(const NetworkRequest& request) {
    return send("POST", request);
}
NetworkReply* NetworkAccessManager::put(const NetworkRequest& request) {
    return send("PUT", request);
}
NetworkReply* NetworkAccessManager::deleteResource(
    const NetworkRequest& request) {
    return send("DELETE", request);
}

void NetworkAccessManager::readCurrent(CurrentRequest* state) {
    bool eof = false;
    const bool ok = state->tls
                        ? state->tls->readAvailable(state->raw, eof)
                        : state->socket->readAvailable(state->raw, eof);
    if (!ok) {
        state->reply->d->error = NetworkReply::Error::NetworkError;
        state->reply->d->errorString = "failed while reading the HTTP response";
        finishCurrent(state);
        return;
    }
    if (eof)
        finishCurrent(state);
}

void NetworkAccessManager::timeoutCurrent(CurrentRequest* state) {
    const bool pending = std::any_of(
        d->currentRequests.begin(), d->currentRequests.end(),
        [state](const std::unique_ptr<CurrentRequest>& candidate) {
            return candidate.get() == state;
        });
    if (!pending)
        return;
    state->reply->d->error = NetworkReply::Error::Timeout;
    state->reply->d->errorString = "request timed out";
    finishCurrent(state);
}

void NetworkAccessManager::finishCurrent(CurrentRequest* state) {
    auto it = std::find_if(
        d->currentRequests.begin(), d->currentRequests.end(),
        [state](const std::unique_ptr<CurrentRequest>& candidate) {
            return candidate.get() == state;
        });
    if (it == d->currentRequests.end())
        return;
    std::unique_ptr<CurrentRequest> owned = std::move(*it);
    d->currentRequests.erase(it);
    if (owned->notifier)
        owned->notifier->setEnabled(false);

    NetworkReply* reply = owned->reply;
    if (reply->d->error == NetworkReply::Error::NoError) {
        const std::string& raw = owned->raw;
        const std::size_t headerEnd = raw.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            reply->d->error = NetworkReply::Error::NetworkError;
            reply->d->errorString = "malformed HTTP response";
        } else {
            const std::string head = raw.substr(0, headerEnd);
            reply->d->body = ByteArray(raw.substr(headerEnd + 4));
            const std::size_t lineEnd = head.find("\r\n");
            const std::string statusLine =
                lineEnd == std::string::npos ? head : head.substr(0, lineEnd);
            const std::size_t space1 = statusLine.find(' ');
            if (space1 != std::string::npos) {
                const std::size_t space2 = statusLine.find(' ', space1 + 1);
                const std::string code = space2 == std::string::npos
                    ? statusLine.substr(space1 + 1)
                    : statusLine.substr(space1 + 1, space2 - space1 - 1);
                reply->d->httpStatus = std::atoi(code.c_str());
            }
            std::size_t pos = lineEnd == std::string::npos ? head.size()
                                                           : lineEnd + 2;
            while (pos < head.size()) {
                const std::size_t next = head.find("\r\n", pos);
                const std::string line = next == std::string::npos
                    ? head.substr(pos) : head.substr(pos, next - pos);
                pos = next == std::string::npos ? head.size() : next + 2;
                const std::size_t colon = line.find(':');
                if (colon == std::string::npos)
                    continue;
                reply->d->headers.emplace_back(
                    String(trimCopy(line.substr(0, colon))),
                    String(trimCopy(line.substr(colon + 1))));
            }
            if (reply->d->httpStatus >= 400) {
                reply->d->error = NetworkReply::Error::HttpError;
                reply->d->errorString =
                    "HTTP " + String::number(reply->d->httpStatus);
            }
        }
    }
    reply->d->finished = true;
    reply->emitSignal(NetworkReply::signalFinished());
}

void NetworkAccessManager::startCurrent(CurrentRequest* state,
                                        const String& method,
                                        const NetworkRequest& request) {
    auto fail = [this, state](const String& text) {
        state->reply->d->error = NetworkReply::Error::NetworkError;
        state->reply->d->errorString = text;
        Timer::singleShot(std::chrono::milliseconds(1), state->reply,
                          [this, state] { finishCurrent(state); });
    };

    UrlParts parts;
    if (!parseUrl(request.url().toStdString(), parts)) {
        state->reply->d->error = NetworkReply::Error::InvalidUrl;
        state->reply->d->errorString = "invalid URL: " + request.url();
        Timer::singleShot(std::chrono::milliseconds(1), state->reply,
                          [this, state] { finishCurrent(state); });
        return;
    }

    std::string requestLine = method.toStdString() + " " + parts.path +
                              " HTTP/1.1\r\nHost: " + parts.host +
                              "\r\nConnection: close\r\n";
    for (const auto& header : request.d->headers)
        requestLine += header.first.toStdString() + ": " +
                       header.second.toStdString() + "\r\n";
    if (!request.body().isEmpty())
        requestLine += "Content-Length: " +
                       std::to_string(request.body().size()) + "\r\n";
    requestLine += "\r\n";
    requestLine += request.body().toStdString();

    const auto connectTimeout = request.connectTimeout();
    const auto transferTimeout = request.transferTimeout();
    const bool hasDeadline = transferTimeout.count() > 0;
    const auto deadline = std::chrono::steady_clock::now() + transferTimeout;

    IoStatus status = IoStatus::Failed;
    if (parts.scheme == "https") {
        state->tls = std::make_unique<TlsSocket>();
        status = state->tls->connectTo(parts.host, parts.port, connectTimeout);
        if (status == IoStatus::Ok)
            status = state->tls->writeAll(requestLine, deadline, hasDeadline);
    } else {
        state->socket = std::make_unique<Socket>();
        status = state->socket->connectTo(parts.host, parts.port, connectTimeout);
        if (status == IoStatus::Ok)
            status = state->socket->writeAll(requestLine, deadline, hasDeadline);
    }
    if (status == IoStatus::TimedOut) {
        state->reply->d->error = NetworkReply::Error::Timeout;
        state->reply->d->errorString = "request timed out";
        Timer::singleShot(std::chrono::milliseconds(1), state->reply,
                          [this, state] { finishCurrent(state); });
        return;
    }
    if (status != IoStatus::Ok) {
        fail("cannot connect to " + String(parts.host.c_str()));
        return;
    }

    try {
        const int fd = state->tls ? state->tls->fd() : state->socket->fd();
        state->notifier = std::make_unique<SocketNotifier>(
            fd, SocketNotifier::Type::Read);
        state->notifier->connect<int>(
            this, "activated", [this, state](int) { readCurrent(state); },
            ConnectionMode::Direct);
    } catch (const std::exception& e) {
        fail(String(e.what()));
        return;
    }

    // The response may never arrive (host accepted, then went silent):
    // bound the wait with a watchdog. timeoutCurrent() first checks the
    // state is still pending, so an early finish makes this a no-op.
    if (hasDeadline) {
        Timer::singleShot(transferTimeout, state->reply,
                          [this, state] { timeoutCurrent(state); });
    }
}

NetworkReply* NetworkAccessManager::send(const String& method,
                                         const NetworkRequest& request) {
    if (d->mode == RequestMode::CurrentThread &&
        thread() != Thread::currentThread())
        throw std::logic_error(
            "Firmir::NetworkAccessManager: CurrentThread requests must be "
            "started from the manager's thread");
    auto* reply = new NetworkReply(this);
    reply->d->url = request.url();

    if (d->mode == RequestMode::CurrentThread) {
        auto state = std::make_unique<CurrentRequest>();
        state->reply = reply;
        CurrentRequest* rawState = state.get();
        d->currentRequests.push_back(std::move(state));
        startCurrent(rawState, method, request);
        return reply;
    }

    // A detached std::thread (not Concurrent::run, whose Future destructor
    // would block until the task completes).
    std::thread([method, request, reply]() {
      auto finish = [reply] {
          reply->d->finished = true;
          reply->emitSignal(NetworkReply::signalFinished());
      };
      try {
        UrlParts parts;
        const std::string url = request.url().toStdString();
        if (!parseUrl(url, parts)) {
            reply->d->error = NetworkReply::Error::InvalidUrl;
            reply->d->errorString = "invalid URL: " + request.url();
            finish();
            return;
        }

        std::string requestLine = method.toStdString() + " " + parts.path +
                                  " HTTP/1.1\r\nHost: " + parts.host +
                                  "\r\nConnection: close\r\n";
        for (const auto& header : request.d->headers)
            requestLine += header.first.toStdString() + ": " +
                           header.second.toStdString() + "\r\n";
        if (!request.body().isEmpty())
            requestLine += "Content-Length: " +
                           std::to_string(request.body().size()) + "\r\n";
        requestLine += "\r\n";
        requestLine += request.body().toStdString();

        std::string raw;
        const auto connectTimeout = request.connectTimeout();
        const auto transferTimeout = request.transferTimeout();
        const bool hasDeadline = transferTimeout.count() > 0;
        const auto deadline = std::chrono::steady_clock::now() + transferTimeout;
        IoStatus status = IoStatus::Failed;
        if (parts.scheme == "https") {
            TlsSocket sock;
            status = sock.connectTo(parts.host, parts.port, connectTimeout);
            if (status == IoStatus::Ok)
                status = sock.writeAll(requestLine, deadline, hasDeadline);
            if (status == IoStatus::Ok)
                status = sock.readAll(raw, deadline, hasDeadline);
        } else {
            Socket sock;
            status = sock.connectTo(parts.host, parts.port, connectTimeout);
            if (status == IoStatus::Ok)
                status = sock.writeAll(requestLine, deadline, hasDeadline);
            if (status == IoStatus::Ok)
                status = sock.readAll(raw, deadline, hasDeadline);
        }

        if (status != IoStatus::Ok) {
            if (status == IoStatus::TimedOut) {
                reply->d->error = NetworkReply::Error::Timeout;
                reply->d->errorString = "request timed out";
            } else if (status == IoStatus::HostNotFound) {
                reply->d->error = NetworkReply::Error::HostNotFound;
                reply->d->errorString =
                    "host not found: " + String(parts.host.c_str());
            } else {
                reply->d->error = NetworkReply::Error::NetworkError;
                reply->d->errorString =
                    "cannot connect to " + String(parts.host.c_str());
            }
            Log(lcNetwork(), LogLevel::Warning)
                << "request failed:" << request.url() << "-"
                << reply->d->errorString;
            finish();
            return;
        }

        // Parse status line and headers.
        const std::size_t headerEnd = raw.find("\r\n\r\n");
        const std::string head =
            headerEnd == std::string::npos ? raw : raw.substr(0, headerEnd);
        reply->d->body = headerEnd == std::string::npos
                             ? ByteArray()
                             : ByteArray(raw.substr(headerEnd + 4));

        const std::size_t lineEnd = head.find("\r\n");
        const std::string statusLine =
            lineEnd == std::string::npos ? head : head.substr(0, lineEnd);
        const std::size_t space1 = statusLine.find(' ');
        if (space1 != std::string::npos) {
            const std::size_t space2 = statusLine.find(' ', space1 + 1);
            const std::string codeStr =
                space2 == std::string::npos
                    ? statusLine.substr(space1 + 1)
                    : statusLine.substr(space1 + 1, space2 - space1 - 1);
            reply->d->httpStatus = std::atoi(codeStr.c_str());
        }

        std::size_t pos =
            lineEnd == std::string::npos ? head.size() : lineEnd + 2;
        while (pos < head.size()) {
            const std::size_t next = head.find("\r\n", pos);
            const std::string line = next == std::string::npos
                                         ? head.substr(pos)
                                         : head.substr(pos, next - pos);
            pos = next == std::string::npos ? head.size() : next + 2;
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos)
                continue;
            reply->d->headers.emplace_back(
                String(trimCopy(line.substr(0, colon))),
                String(trimCopy(line.substr(colon + 1))));
        }

        if (reply->d->httpStatus >= 400) {
            reply->d->error = NetworkReply::Error::HttpError;
            reply->d->errorString =
                "HTTP " + String::number(reply->d->httpStatus);
        }
        finish();
      } catch (const std::exception& e) {
        reply->d->error = NetworkReply::Error::NetworkError;
        reply->d->errorString = String(e.what());
        finish();
      } catch (...) {
        reply->d->error = NetworkReply::Error::NetworkError;
        reply->d->errorString = "unknown network error";
        finish();
      }
    }).detach();

    return reply;
}

NetworkRequest::NetworkRequest(const String& url)
    : d(std::make_unique<Impl>()) {
    d->url = url;
}

NetworkRequest::~NetworkRequest() = default;
NetworkRequest::NetworkRequest(const NetworkRequest& other)
    : d(std::make_unique<Impl>(*other.d)) {}
NetworkRequest::NetworkRequest(NetworkRequest&&) noexcept = default;
NetworkRequest& NetworkRequest::operator=(const NetworkRequest& other) {
    if (this != &other)
        *d = *other.d;
    return *this;
}
NetworkRequest& NetworkRequest::operator=(NetworkRequest&&) noexcept = default;

String NetworkRequest::url() const { return d->url; }
void NetworkRequest::setUrl(const String& url) { d->url = url; }

void NetworkRequest::setHeader(const String& name, const String& value) {
    for (auto& pair : d->headers) {
        if (pair.first == name) {
            pair.second = value;
            return;
        }
    }
    d->headers.emplace_back(name, value);
}

String NetworkRequest::header(const String& name) const {
    for (const auto& pair : d->headers) {
        if (pair.first == name)
            return pair.second;
    }
    return String();
}

ByteArray NetworkRequest::body() const { return d->body; }
void NetworkRequest::setBody(const ByteArray& body) { d->body = body; }

void NetworkRequest::setConnectTimeout(std::chrono::milliseconds timeout) {
    d->connectTimeout = timeout;
}
std::chrono::milliseconds NetworkRequest::connectTimeout() const {
    return d->connectTimeout;
}

void NetworkRequest::setTransferTimeout(std::chrono::milliseconds timeout) {
    d->transferTimeout = timeout;
}
std::chrono::milliseconds NetworkRequest::transferTimeout() const {
    return d->transferTimeout;
}

} // namespace Firmir
