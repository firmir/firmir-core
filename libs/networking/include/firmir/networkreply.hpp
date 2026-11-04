#pragma once

// Firmir::NetworkReply — the result of an HTTP request, the analogue of
// QNetworkReply. Created by NetworkAccessManager; deleteLater()-safe.

#include <firmir/bytearray.hpp>
#include <firmir/object.hpp>
#include <firmir/string.hpp>

#include <memory>
#include <vector>

namespace Firmir {

/// The result of a network request.
class NetworkReply : public Object {
public:
    enum class Error {
        NoError,
        ConnectionRefused,
        HostNotFound,
        Timeout,
        NetworkError,
        InvalidUrl,
        HttpError // status >= 400; httpStatus() carries the code
    };

    ~NetworkReply() override;

    NetworkReply(const NetworkReply&) = delete;
    NetworkReply& operator=(const NetworkReply&) = delete;

    /// True when the reply is complete (successfully or with an error).
    bool isFinished() const;
    Error error() const;
    /// Human-readable error description.
    String errorString() const;
    /// The HTTP status code, or 0 when not applicable.
    int httpStatus() const;
    /// The response body.
    ByteArray readAll() const;
    /// Response headers (lower-cased name -> value).
    std::vector<std::pair<String, String>> headers() const;
    /// Returns the value of a response header, or "" when missing.
    String header(const String& name) const;
    /// The final URL after redirects.
    String url() const;

    /// Signal (via Object::connect): fired when the reply is complete
    /// (successfully or with an error).
    static constexpr const char* signalFinished() { return "finished()"; }

private:
    friend class NetworkAccessManager;
    explicit NetworkReply(Object* parent = nullptr);

    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
