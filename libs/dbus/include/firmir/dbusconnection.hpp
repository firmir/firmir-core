#pragma once

// Firmir::DBusConnection — a D-Bus connection implemented from the wire
// specification: unix-socket transport, SASL EXTERNAL authentication and the
// message protocol (see dbusmessage.hpp). Supports blocking method calls,
// signal subscriptions and hosting service objects.
//
//   DBusConnection bus(DBusConnection::BusType::Session);
//   DBusMessage reply = bus.call(DBusMessage::createMethodCall(
//       "org.freedesktop.DBus", "/org/freedesktop/DBus",
//       "org.freedesktop.DBus", "ListNames"));
//   DBusMessage* pending = bus.asyncCall(DBusMessage::createMethodCall(...));
//   pending->connect(context, DBusMessage::signalFinished(), [pending] {
//       // pending now contains the method return or error reply.
//   });
//
// Initially a dedicated reader thread dispatches incoming messages. Signal
// handlers and method handlers run in that reader thread; marshall into your
// own thread (e.g. with queued Firmir signals) when needed. The first
// asyncCall() stops the reader and transfers descriptor polling to a
// SocketNotifier in the connection owner's event loop. Blocking call() remains
// available in either mode.

#include <firmir/dbusmessage.hpp>
#include <firmir/object.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace Firmir {

struct DBusArgInfo {
    String name;
    std::string signature;
};

struct DBusMethodInfo {
    String name;
    std::vector<DBusArgInfo> inArgs;
    std::vector<DBusArgInfo> outArgs;
};

struct DBusSignalInfo {
    String name;
    std::vector<DBusArgInfo> args;
};

struct DBusInterfaceInfo {
    String name;
    std::vector<DBusMethodInfo> methods;
    std::vector<DBusSignalInfo> signals;
};

class DBusConnection : public Object {
public:
    enum class BusType { Session, System };

    /// Connects to the bus immediately (session: DBUS_SESSION_BUS_ADDRESS,
    /// system: /var/run/dbus/system_bus_socket), authenticates and says
    /// Hello. isConnected() reports the outcome.
    explicit DBusConnection(BusType bus = BusType::Session,
                            Object* parent = nullptr);
    ~DBusConnection() override;

    DBusConnection(const DBusConnection&) = delete;
    DBusConnection& operator=(const DBusConnection&) = delete;

    bool isConnected() const;
    /// The unique name assigned by the bus (e.g. ":1.42").
    String uniqueName() const;

    /// Blocking method call. Throws DBusError on an error reply,
    /// std::runtime_error on transport failure or timeout.
    DBusMessage call(const DBusMessage& message, int timeoutMs = 25000);
    /// Starts a non-blocking method call in the connection owner's thread.
    /// The returned pending message is owned by this connection. When the
    /// reply arrives its contents replace the pending message and
    /// DBusMessage::signalFinished() is emitted. Error replies are delivered
    /// through the same signal; no exception is thrown by the callback.
    /// Incoming data is read through SocketNotifier once asyncCall() is used.
    DBusMessage* asyncCall(const DBusMessage& message, int timeoutMs = 25000);
    /// Fire-and-forget send (for signals and no-reply calls).
    bool send(const DBusMessage& message);

    /// Acquires a well-known name on the bus. Thread-safe.
    bool requestName(const String& name);

    /// Adds a dbus match rule (org.freedesktop.DBus.AddMatch).
    bool addMatch(const String& rule);
    /// Subscribes to signals with the given interface/member; empty strings
    /// match everything. The handler runs in the reader thread.
    using SignalHandler = std::function<void(const DBusMessage&)>;
    void subscribeSignal(const String& interface, const String& member,
                         SignalHandler handler);

    /// Hosts an object at @p path. Method calls are routed to @p handler
    /// (reader thread); org.freedesktop.DBus.Introspectable.Introspect is
    /// answered automatically from @p interfaces.
    using MethodHandler = std::function<DBusMessage(const DBusMessage&)>;
    void registerObject(const String& path,
                        std::vector<DBusInterfaceInfo> interfaces,
                        MethodHandler handler);

    /// The introspection XML generated for a registered path (""
    /// when nothing is registered there).
    String introspectXml(const String& path) const;

    /// Convenience: emits a signal from a hosted object.
    bool emitDBusSignal(const String& path, const String& interface,
                        const String& name, std::vector<DBusValue> args);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
