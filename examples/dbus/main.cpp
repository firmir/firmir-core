// D-Bus example: hosts a tiny echo service on a private session bus and
// calls it from a second connection — a full client/server round-trip
// through the real wire protocol.
//
// A private `dbus-daemon --session` instance is spawned for the demo (same
// approach the test suite uses), so no system session bus is required.

#include <firmir/dbusconnection.hpp>
#include <firmir/log.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <signal.h>
#include <string>
#include <unistd.h>

namespace {

const Firmir::LoggingCategory& lcApp() {
    static const Firmir::LoggingCategory category("app.dbus");
    return category;
}

// RAII wrapper around a private dbus-daemon instance.
struct PrivateBus {
    std::string address;
    pid_t pid = -1;

    bool start() {
        FILE* fp = ::popen("dbus-daemon --session --print-address=1 "
                           "--print-pid=1 --fork 2>/dev/null",
                           "r");
        if (!fp)
            return false;
        char line[512];
        if (std::fgets(line, sizeof(line), fp)) {
            address = line;
            while (!address.empty() &&
                   (address.back() == '\n' || address.back() == '\r'))
                address.pop_back();
        }
        if (std::fgets(line, sizeof(line), fp))
            pid = std::atoi(line);
        ::pclose(fp);
        if (address.empty() || pid <= 0)
            return false;
        ::setenv("DBUS_SESSION_BUS_ADDRESS", address.c_str(), 1);
        return true;
    }

    ~PrivateBus() {
        if (pid > 0)
            ::kill(pid, SIGTERM);
    }
};

} // namespace

int main() {
    using namespace Firmir;

    PrivateBus bus;
    if (!bus.start()) {
        cLogError(lcApp()) << "dbus-daemon is unavailable, cannot run the demo";
        return 1;
    }
    cLogInfo(lcApp()) << "private bus at " << String(bus.address);

    // Server side: host /org/firmir/Echo with one method Echo(s)->s.
    DBusConnection server;
    if (!server.isConnected() || !server.requestName("org.firmir.Echo")) {
        cLogError(lcApp()) << "server connection failed";
        return 1;
    }

    DBusInterfaceInfo iface;
    iface.name = "org.firmir.Echo";
    iface.methods.push_back({"Echo",
                             {{"text", "s"}},
                             {{"reply", "s"}}});
    server.registerObject(
        "/org/firmir/Echo", {iface},
        [](const DBusMessage& call) -> DBusMessage {
            if (call.member() == "Echo") {
                DBusMessage reply = DBusMessage::createMethodReturn(call);
                reply.setArguments(call.arguments());
                return reply;
            }
            return DBusMessage::createError(
                call, "org.freedesktop.DBus.Error.UnknownMethod",
                "no such method");
        });

    // Client side: blocking call through the second connection.
    DBusConnection client;
    if (!client.isConnected()) {
        cLogError(lcApp()) << "client connection failed";
        return 1;
    }
    cLogInfo(lcApp()) << "client unique name: " << client.uniqueName();

    DBusMessage call = DBusMessage::createMethodCall(
        "org.firmir.Echo", "/org/firmir/Echo", "org.firmir.Echo", "Echo");
    call.setArguments({DBusValue(String("hello over the bus"))});

    try {
        const DBusMessage reply = client.call(call, 5000);
        const String echoed = reply.arguments().empty()
                                  ? String()
                                  : reply.arguments().front().toString();
        cLogInfo(lcApp()) << ("Echo replied: '" + echoed + "'");

        // Introspection is answered automatically for registered objects.
        const DBusMessage xml = client.call(
            DBusMessage::createMethodCall("org.firmir.Echo",
                                          "/org/firmir/Echo",
                                          "org.freedesktop.DBus.Introspectable",
                                          "Introspect"),
            5000);
        cLogInfo(lcApp()) << "introspection XML:\n"
                          << xml.arguments().front().toString();
        return echoed == "hello over the bus" ? 0 : 1;
    } catch (const std::exception& e) {
        cLogError(lcApp()) << "call failed: " << e.what();
        return 1;
    }
}
