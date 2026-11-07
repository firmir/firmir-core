#pragma once

// Firmir::DBusMessage — a D-Bus message (method call, method return, error
// or signal) with full wire serialization, implemented from the D-Bus
// specification (little-endian, type-driven alignment).

#include <firmir/bytearray.hpp>
#include <firmir/dbuserror.hpp>
#include <firmir/dbusvalue.hpp>
#include <firmir/object.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Firmir {

class DBusMessage : public Object {
public:
    enum class Type : int {
        Invalid = 0,
        MethodCall = 1,
        MethodReturn = 2,
        Error = 3,
        Signal = 4
    };

    DBusMessage();
    ~DBusMessage();
    DBusMessage(const DBusMessage& other);
    DBusMessage(DBusMessage&& other) noexcept;
    DBusMessage& operator=(const DBusMessage& other);
    DBusMessage& operator=(DBusMessage&& other) noexcept;

    /// Emitted once on a pending message returned by DBusConnection::asyncCall.
    /// At emission time this object contains the method return or error reply.
    static constexpr const char* signalFinished() { return "finished()"; }

    static DBusMessage createMethodCall(const String& destination,
                                        const String& path,
                                        const String& interface,
                                        const String& method);
    /// Builds the reply to @p call (its sender becomes the destination).
    static DBusMessage createMethodReturn(const DBusMessage& call);
    static DBusMessage createError(const DBusMessage& call,
                                   const String& errorName,
                                   const String& message);
    static DBusMessage createSignal(const String& path,
                                    const String& interface,
                                    const String& name);

    Type type() const;

    String path() const;
    String interface() const;
    String member() const;
    String destination() const;
    String sender() const;
    String errorName() const;

    void setSender(const String& sender);
    void setDestination(const String& destination);

    std::uint32_t serial() const;
    void setSerial(std::uint32_t serial);
    std::uint32_t replySerial() const;
    void setReplySerial(std::uint32_t serial);

    /// The concatenated type signature of the arguments.
    std::string argumentsSignature() const;
    const std::vector<DBusValue>& arguments() const;
    void setArguments(std::vector<DBusValue> arguments);

    /// Serializes the message (serial must be assigned already).
    ByteArray serialize() const;

    /// Parses one message from the front of the buffer.
    /// Returns false (without touching @p out, @p consumed set to 0) when
    /// the buffer does not yet hold a complete message. On malformed input
    /// returns false and sets @p error when given.
    static bool parse(const char* data, std::size_t size, DBusMessage& out,
                      std::size_t& consumed, String* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

/// Builds a DBusError from an Error message (name + first string argument).
DBusError dbusErrorFromMessage(const DBusMessage& message);

} // namespace Firmir
