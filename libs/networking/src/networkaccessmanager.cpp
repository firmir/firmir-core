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
#include <fcntl.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace Firmir {

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

/// A tiny blocking TCP socket wrapper.
class Socket {
public:
    ~Socket() { close(); }

    int fd() const { return fd_; }

    bool connectTo(const std::string& host, int port) {
        struct addrinfo hints {};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo* result = nullptr;
        if (::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                          &result) != 0)
            return false;
        for (struct addrinfo* rp = result; rp; rp = rp->ai_next) {
            fd_ = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (fd_ == -1)
                continue;
            if (::connect(fd_, rp->ai_addr, rp->ai_addrlen) == 0)
                break;
            ::close(fd_);
            fd_ = -1;
        }
        ::freeaddrinfo(result);
        return fd_ != -1;
    }

    bool writeAll(const std::string& data) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            const ssize_t n = ::send(fd_, data.data() + sent,
                                     data.size() - sent, MSG_NOSIGNAL);
            if (n <= 0)
                return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    /// Reads until EOF; suitable for "Connection: close" responses.
    bool readAll(std::string& out) {
        char buf[8192];
        for (;;) {
            const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
            if (n < 0)
                return false;
            if (n == 0)
                return true;
            out.append(buf, static_cast<std::size_t>(n));
        }
    }

    bool setNonBlocking() {
        const int flags = ::fcntl(fd_, F_GETFL, 0);
        return flags >= 0 && ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) == 0;
    }

    /// Drains currently available bytes without waiting for more data.
    bool readAvailable(std::string& out, bool& eof) {
        char buf[8192];
        for (;;) {
            const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
            if (n > 0) {
                out.append(buf, static_cast<std::size_t>(n));
                continue;
            }
            if (n == 0) {
                eof = true;
                return true;
            }
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return true;
            return false;
        }
    }

    void close() {
        if (fd_ != -1) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};
/// A minimal TLS wrapper around OpenSSL.
class TlsSocket {
public:
    ~TlsSocket() { close(); }

    bool connectTo(const std::string& host, int port) {
        if (!socket_.connectTo(host, port))
            return false;

        static SSL_CTX* ctx = [] {
            SSL_library_init();
            SSL_CTX* c = SSL_CTX_new(TLS_client_method());
            SSL_CTX_set_default_verify_paths(c);
            return c;
        }();
        if (!ctx)
            return false;

        ssl_ = SSL_new(ctx);
        if (!ssl_)
            return false;
        SSL_set_fd(ssl_, socket_.fd());
        SSL_set_tlsext_host_name(ssl_, host.c_str());
        if (SSL_connect(ssl_) != 1) {
            close();
            return false;
        }
        return true;
    }

    bool writeAll(const std::string& data) {
        std::size_t sent = 0;
        while (sent < data.size()) {
            const int n = SSL_write(ssl_, data.data() + sent,
                                    static_cast<int>(data.size() - sent));
            if (n <= 0)
                return false;
            sent += static_cast<std::size_t>(n);
        }
        return true;
    }

    bool readAll(std::string& out) {
        char buf[8192];
        for (;;) {
            const int n = SSL_read(ssl_, buf, sizeof(buf));
            if (n < 0)
                return false;
            if (n == 0)
                return true;
            out.append(buf, static_cast<std::size_t>(n));
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
                (errno == EAGAIN || errno == EWOULDBLOCK))
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

    bool ok = false;
    if (parts.scheme == "https") {
        state->tls = std::make_unique<TlsSocket>();
        ok = state->tls->connectTo(parts.host, parts.port) &&
             state->tls->writeAll(requestLine) && state->tls->setNonBlocking();
    } else {
        state->socket = std::make_unique<Socket>();
        ok = state->socket->connectTo(parts.host, parts.port) &&
             state->socket->writeAll(requestLine) &&
             state->socket->setNonBlocking();
    }
    if (!ok) {
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
        bool ok = false;
        if (parts.scheme == "https") {
            TlsSocket sock;
            ok = sock.connectTo(parts.host, parts.port) &&
                 sock.writeAll(requestLine) && sock.readAll(raw);
        } else {
            Socket sock;
            ok = sock.connectTo(parts.host, parts.port) &&
                 sock.writeAll(requestLine) && sock.readAll(raw);
        }

        if (!ok) {
            reply->d->error = NetworkReply::Error::NetworkError;
            reply->d->errorString =
                "cannot connect to " + String(parts.host.c_str());
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

} // namespace Firmir
