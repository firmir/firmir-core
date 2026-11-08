#pragma once

// Firmir::NetworkAccessManager — asynchronous HTTP client, the analogue
// Only http:// and https:// URLs are supported;
// GET, POST, PUT and DELETE. WorkerThread (the default) runs requests on a
// background thread. CurrentThread connects/writes in the caller and receives
// the response through SocketNotifier in the manager's event loop.

#include <firmir/bytearray.hpp>
#include <firmir/object.hpp>
#include <firmir/string.hpp>

#include <chrono>
#include <memory>
#include <utility>
#include <vector>

namespace Firmir {

class NetworkReply;

/// An HTTP request: a URL plus optional headers and a body.
class NetworkRequest {
public:
    explicit NetworkRequest(const String& url = String());
    ~NetworkRequest();
    NetworkRequest(const NetworkRequest& other);
    NetworkRequest(NetworkRequest&& other) noexcept;
    NetworkRequest& operator=(const NetworkRequest& other);
    NetworkRequest& operator=(NetworkRequest&& other) noexcept;

    String url() const;
    void setUrl(const String& url);

    void setHeader(const String& name, const String& value);
    String header(const String& name) const;

    ByteArray body() const;
    void setBody(const ByteArray& body);

    /// Bounds the TCP connect + TLS handshake. The default is 10 s;
    /// std::chrono::milliseconds(0) disables the timeout. On expiry the
    /// reply finishes with NetworkReply::Error::Timeout.
    void setConnectTimeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds connectTimeout() const;
    /// Bounds the whole request (connect, send, receive). The default is
    /// 30 s; std::chrono::milliseconds(0) disables the timeout. On expiry
    /// the reply finishes with NetworkReply::Error::Timeout.
    void setTransferTimeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds transferTimeout() const;

private:
    friend class NetworkAccessManager;
    struct Impl;
    std::unique_ptr<Impl> d;
};

/// Dispatches network requests and owns the replies.
class NetworkAccessManager : public Object {
public:
    enum class RequestMode {
        CurrentThread, ///< SocketNotifier-driven response reads in the owner's thread.
        WorkerThread   ///< The whole request runs on a detached worker thread.
    };

    explicit NetworkAccessManager(Object* parent = nullptr);
    ~NetworkAccessManager() override;

    NetworkAccessManager(const NetworkAccessManager&) = delete;
    NetworkAccessManager& operator=(const NetworkAccessManager&) = delete;

    /// Selects how subsequent requests are executed. The default is WorkerThread.
    void setRequestMode(RequestMode mode);
    RequestMode requestMode() const;

    NetworkReply* get(const NetworkRequest& request);
    NetworkReply* post(const NetworkRequest& request);
    NetworkReply* put(const NetworkRequest& request);
    NetworkReply* deleteResource(const NetworkRequest& request);

private:
    NetworkReply* send(const String& method, const NetworkRequest& request);

    struct CurrentRequest;
    void startCurrent(CurrentRequest* requestState, const String& method,
                      const NetworkRequest& request);
    void readCurrent(CurrentRequest* request);
    void finishCurrent(CurrentRequest* request);
    /// Marks the request as timed out and finishes it; a no-op when the
    /// request already completed (the watchdog timer may fire late).
    void timeoutCurrent(CurrentRequest* request);

    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
