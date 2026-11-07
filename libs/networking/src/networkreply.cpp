#include <firmir/networkreply.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>

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

NetworkReply::NetworkReply(Object* parent)
    : Object(parent), d(std::make_unique<Impl>()) {}

NetworkReply::~NetworkReply() = default;

bool NetworkReply::isFinished() const { return d->finished.load(); }
NetworkReply::Error NetworkReply::error() const { return d->error; }
String NetworkReply::errorString() const { return d->errorString; }
int NetworkReply::httpStatus() const { return d->httpStatus; }
ByteArray NetworkReply::readAll() const { return d->body; }

std::vector<std::pair<String, String>> NetworkReply::headers() const {
    return d->headers;
}

String NetworkReply::header(const String& name) const {
    const std::string needle = name.toLower().toStdString();
    for (const auto& [n, v] : d->headers) {
        if (n.toLower().toStdString() == needle)
            return v;
    }
    return String();
}

String NetworkReply::url() const { return d->url; }

} // namespace Firmir
