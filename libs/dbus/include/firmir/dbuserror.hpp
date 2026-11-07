#pragma once

#include <firmir/string.hpp>

#include <memory>
#include <stdexcept>

namespace Firmir {

/// A D-Bus error: an error name (dotted, e.g. "org.freedesktop.DBus.Error.
/// Failed") plus a human-readable message. Thrown by blocking calls.
class DBusError : public std::runtime_error {
public:
    DBusError(String name, String message);
    ~DBusError() override;
    DBusError(const DBusError& other);
    DBusError(DBusError&& other) noexcept;
    DBusError& operator=(const DBusError& other);
    DBusError& operator=(DBusError&& other) noexcept;

    /// The dotted error name.
    String name() const;
    /// The error text (same as what()).
    String message() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
