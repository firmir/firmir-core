#include <firmir/dbuserror.hpp>

namespace Firmir {

struct DBusError::Impl {
    String name;
    String message;
};

DBusError::DBusError(String name, String message)
    : std::runtime_error(message.toStdString()), d(std::make_unique<Impl>()) {
    d->name = std::move(name);
    d->message = std::move(message);
}

DBusError::~DBusError() = default;

DBusError::DBusError(const DBusError& other)
    : std::runtime_error(other), d(std::make_unique<Impl>(*other.d)) {}

DBusError::DBusError(DBusError&&) noexcept = default;

DBusError& DBusError::operator=(const DBusError& other) {
    if (this != &other) {
        std::runtime_error::operator=(other);
        d->name = other.d->name;
        d->message = other.d->message;
    }
    return *this;
}

DBusError& DBusError::operator=(DBusError&&) noexcept = default;

String DBusError::name() const { return d->name; }
String DBusError::message() const { return d->message; }

} // namespace Firmir
