#include <firmir/dbusconnection.hpp>

#include <firmir/socketnotifier.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace Firmir {

namespace {

std::string hexEncodeUid() {
    const std::string uid = std::to_string(::getuid());
    std::string hex;
    const char* digits = "0123456789abcdef";
    for (const char c : uid) {
        hex += digits[(c >> 4) & 0xF];
        hex += digits[c & 0xF];
    }
    return hex;
}

std::string busAddress(DBusConnection::BusType bus) {
    if (bus == DBusConnection::BusType::Session) {
        const char* env = std::getenv("DBUS_SESSION_BUS_ADDRESS");
        if (env)
            return env;
        return {};
    }
    return "unix:path=/var/run/dbus/system_bus_socket";
}

String escapeXml(const String& text) {
    String out;
    for (const char c : text.toStdString()) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += String(1, c); break;
        }
    }
    return out;
}

String buildIntrospectionXml(const std::vector<DBusInterfaceInfo>& ifaces) {
    String xml = "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object "
                 "Introspection 1.0//EN\" \"http://www.freedesktop.org/"
                 "standards/dbus/1.0/introspect.dtd\">\n<node>\n";
    for (const DBusInterfaceInfo& iface : ifaces) {
        xml += "  <interface name=\"" + escapeXml(iface.name) + "\">\n";
        for (const DBusMethodInfo& method : iface.methods) {
            xml += "    <method name=\"" + escapeXml(method.name) + "\">\n";
            for (const DBusArgInfo& arg : method.inArgs)
                xml += "      <arg name=\"" + escapeXml(arg.name) +
                       "\" type=\"" + String(arg.signature) +
                       "\" direction=\"in\"/>\n";
            for (const DBusArgInfo& arg : method.outArgs)
                xml += "      <arg name=\"" + escapeXml(arg.name) +
                       "\" type=\"" + String(arg.signature) +
                       "\" direction=\"out\"/>\n";
            xml += "    </method>\n";
        }
        for (const DBusSignalInfo& signal : iface.signals) {
            xml += "    <signal name=\"" + escapeXml(signal.name) + "\">\n";
            for (const DBusArgInfo& arg : signal.args)
                xml += "      <arg name=\"" + escapeXml(arg.name) +
                       "\" type=\"" + String(arg.signature) + "\"/>\n";
            xml += "    </signal>\n";
        }
        xml += "  </interface>\n";
    }
    xml += "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
           "    <method name=\"Introspect\">\n"
           "      <arg name=\"xml_data\" type=\"s\" direction=\"out\"/>\n"
           "    </method>\n"
           "  </interface>\n"
           "</node>\n";
    return xml;
}

} // namespace

class DBusConnection::Impl {
public:
    Impl(BusType bus, DBusConnection* owner) : owner(owner) { connectTo(bus); }

    ~Impl() {
        notifier.reset();
        stopping.store(true);
        readerStop.store(true);
        if (fd >= 0)
            ::shutdown(fd, SHUT_RDWR);
        if (reader.joinable())
            reader.join();
        if (fd >= 0)
            ::close(fd);
    }

    void connectTo(BusType bus) {
        // A bus address may carry extra comma-separated key=value parameters
        // (e.g. "unix:path=/tmp/dbus-XXXX,guid=..."); keep the first
        // transport entry only.
        std::string address = busAddress(bus);
        const std::size_t comma = address.find(',');
        if (comma != std::string::npos)
            address.resize(comma);
        const std::string prefixPath = "unix:path=";
        const std::string prefixAbstract = "unix:abstract=";

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::size_t addrLen = sizeof(addr);
        if (address.compare(0, prefixPath.size(), prefixPath) == 0) {
            const std::string path = address.substr(prefixPath.size());
            if (path.size() >= sizeof(addr.sun_path))
                return;
            std::strcpy(addr.sun_path, path.c_str());
        } else if (address.compare(0, prefixAbstract.size(),
                                   prefixAbstract) == 0) {
            const std::string name = address.substr(prefixAbstract.size());
            if (name.size() >= sizeof(addr.sun_path) - 1)
                return;
            addr.sun_path[0] = '\0';
            std::memcpy(addr.sun_path + 1, name.data(), name.size());
            addrLen = sizeof(sa_family_t) + 1 + name.size();
        } else {
            return; // Unsupported address family (tcp:, launchd:, ...).
        }

        fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        if (fd < 0)
            return;
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), addrLen) < 0) {
            ::close(fd);
            fd = -1;
            return;
        }
        if (!authenticate()) {
            ::close(fd);
            fd = -1;
            return;
        }
        connected = true;
        reader = std::thread([this] { readerLoop(); });

        // Hello: obtains the unique name.
        const DBusMessage hello = DBusMessage::createMethodCall(
            "org.freedesktop.DBus", "/org/freedesktop/DBus",
            "org.freedesktop.DBus", "Hello");
        try {
            const DBusMessage reply = call(hello, 5000);
            if (!reply.arguments().empty())
                uniqueName = reply.arguments().front().toString();
        } catch (...) {
            connected = false;
        }
    }

    bool authenticate() {
        // SASL EXTERNAL with the hex-encoded uid.
        const std::string auth =
            std::string("\0AUTH EXTERNAL ", 15) + hexEncodeUid() + "\r\n";
        if (!writeAll(auth.data(), auth.size()))
            return false;
        std::string reply;
        while (reply.size() < 4096) { // Read one CRLF-terminated line.
            char c = 0;
            if (::read(fd, &c, 1) != 1)
                return false;
            reply += c;
            if (reply.size() >= 2 &&
                reply.compare(reply.size() - 2, 2, "\r\n") == 0)
                break;
        }
        if (reply.compare(0, 3, "OK ") != 0)
            return false;
        const std::string begin = "BEGIN\r\n";
        return writeAll(begin.data(), begin.size());
    }

    bool writeAll(const char* data, std::size_t size) {
        std::lock_guard<std::mutex> lk(writeMutex);
        while (size > 0) {
            const ssize_t n = ::send(fd, data, size, MSG_NOSIGNAL);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                return false;
            }
            data += n;
            size -= static_cast<std::size_t>(n);
        }
        return true;
    }

    struct PendingCall {
        bool done = false;
        DBusMessage reply;
        std::string error;
    };

    struct AsyncCall {
        DBusMessage* message = nullptr;
        Timer* timeout = nullptr;
    };

    void finishAsync(const AsyncCall& call, const DBusMessage& reply) {
        if (call.timeout)
            call.timeout->stop();
        *call.message = reply;
        call.message->emitSignal(DBusMessage::signalFinished());
    }

    void finishAsyncError(const AsyncCall& call, const String& name,
                          const String& text) {
        finishAsync(call, DBusMessage::createError(*call.message, name, text));
    }

    void handleDisconnect() {
        connected = false;
        if (notifier)
            notifier->setEnabled(false);
        std::vector<AsyncCall> async;
        {
            std::lock_guard<std::mutex> lk(pendingMutex);
            for (auto& [serial, pending] : pendingCalls) {
                (void)serial;
                pending->error = "connection lost";
                pending->done = true;
            }
            for (auto& [serial, call] : asyncCalls) {
                (void)serial;
                async.push_back(call);
            }
            asyncCalls.clear();
        }
        pendingCv.notify_all();
        for (const AsyncCall& call : async)
            finishAsyncError(call, "org.firmir.DBus.Error.Disconnected",
                             "connection lost");
    }

    bool readAvailable() {
        char chunk[16384];
        const ssize_t n = ::read(fd, chunk, sizeof(chunk));
        if (n <= 0) {
            if (n < 0 && (errno == EINTR || errno == EAGAIN ||
                          errno == EWOULDBLOCK))
                return true;
            handleDisconnect();
            return false;
        }
        readBuffer.append(chunk, static_cast<std::size_t>(n));
        std::size_t offset = 0;
        while (offset < readBuffer.size()) {
            DBusMessage msg;
            std::size_t used = 0;
            String error;
            if (!DBusMessage::parse(readBuffer.data() + offset,
                                    readBuffer.size() - offset, msg, used,
                                    &error)) {
                if (!error.isEmpty()) {
                    handleDisconnect();
                    return false;
                }
                break;
            }
            offset += used;
            dispatch(msg);
        }
        readBuffer.erase(0, offset);
        return true;
    }

    void startNotifier() {
        if (!eventDriven) {
            if (reader.joinable() &&
                reader.get_id() == std::this_thread::get_id())
                throw std::logic_error(
                    "Firmir::DBusConnection: asyncCall cannot switch to a "
                    "SocketNotifier from its own reader callback");
            readerStop.store(true);
            if (reader.joinable())
                reader.join();
            eventDriven = true;
        }
        if (!notifier) {
            notifier = std::make_unique<SocketNotifier>(
                fd, SocketNotifier::Type::Read);
            notifier->connect<int>(
                owner, "activated", [this](int) { readAvailable(); },
                ConnectionMode::Direct);
        }
    }

    DBusMessage* asyncCall(const DBusMessage& message, int timeoutMs) {
        if (owner->thread() != Thread::currentThread())
            throw std::logic_error(
                "Firmir::DBusConnection: asyncCall must be called from the "
                "connection owner's thread");
        if (!connected)
            throw std::runtime_error("Firmir::DBusConnection: not connected");
        startNotifier();

        DBusMessage msg = message;
        const std::uint32_t serial = nextSerial++;
        msg.setSerial(serial);
        auto* pending = new DBusMessage(msg);
        pending->moveToThread(owner->thread());
        pending->setParent(owner);
        AsyncCall call;
        call.message = pending;
        {
            std::lock_guard<std::mutex> lk(pendingMutex);
            asyncCalls.emplace(serial, call);
        }

        const ByteArray wire = msg.serialize();
        if (!writeAll(wire.constData(),
                      static_cast<std::size_t>(wire.size()))) {
            std::lock_guard<std::mutex> lk(pendingMutex);
            asyncCalls.erase(serial);
            delete pending;
            throw std::runtime_error("Firmir::DBusConnection: write failed");
        }

        if (timeoutMs > 0) {
            auto* timer = new Timer(pending);
            timer->setSingleShot(true);
            timer->setInterval(std::chrono::milliseconds(timeoutMs));
            timer->connect(
                pending, "timeout",
                [this, serial] {
                    AsyncCall timedOut;
                    {
                        std::lock_guard<std::mutex> lk(pendingMutex);
                        auto it = asyncCalls.find(serial);
                        if (it == asyncCalls.end())
                            return;
                        timedOut = it->second;
                        asyncCalls.erase(it);
                    }
                    finishAsyncError(timedOut, "org.firmir.DBus.Error.Timeout",
                                     "D-Bus call timed out");
                },
                ConnectionMode::Direct);
            {
                std::lock_guard<std::mutex> lk(pendingMutex);
                asyncCalls.at(serial).timeout = timer;
            }
            timer->start();
        }
        return pending;
    }

    DBusMessage call(const DBusMessage& message, int timeoutMs) {
        if (!connected)
            throw std::runtime_error("Firmir::DBusConnection: not connected");
        if (eventDriven && owner->thread() != Thread::currentThread())
            throw std::logic_error(
                "Firmir::DBusConnection: notifier-driven call must run in the "
                "connection owner's thread");
        DBusMessage msg = message;
        const std::uint32_t serial = nextSerial++;
        msg.setSerial(serial);

        auto pending = std::make_shared<PendingCall>();
        {
            std::lock_guard<std::mutex> lk(pendingMutex);
            pendingCalls.emplace(serial, pending);
        }
        const ByteArray wire = msg.serialize();
        if (!writeAll(wire.constData(),
                      static_cast<std::size_t>(wire.size()))) {
            std::lock_guard<std::mutex> lk(pendingMutex);
            pendingCalls.erase(serial);
            throw std::runtime_error("Firmir::DBusConnection: write failed");
        }

        bool done = false;
        if (eventDriven) {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(timeoutMs);
            while (true) {
                {
                    std::lock_guard<std::mutex> lk(pendingMutex);
                    if (pending->done) {
                        done = true;
                        break;
                    }
                }
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline)
                    break;
                const int remaining = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - now).count());
                pollfd pfd{fd, POLLIN, 0};
                const int rc = ::poll(&pfd, 1, remaining > 0 ? remaining : 1);
                if (rc < 0 && errno == EINTR)
                    continue;
                if (rc < 0)
                    break;
                if (rc > 0)
                    readAvailable();
            }
        } else {
            std::unique_lock<std::mutex> lk(pendingMutex);
            done = pendingCv.wait_for(
                lk, std::chrono::milliseconds(timeoutMs),
                [&] { return pending->done; });
        }
        {
            std::lock_guard<std::mutex> lk(pendingMutex);
            pendingCalls.erase(serial);
        }
        if (!done)
            throw std::runtime_error(
                "Firmir::DBusConnection: call timed out");
        if (!pending->error.empty())
            throw std::runtime_error(pending->error);
        if (pending->reply.type() == DBusMessage::Type::Error)
            throw dbusErrorFromMessage(pending->reply);
        return pending->reply;
    }

    void readerLoop() {
        std::string buffer;
        while (!stopping.load() && !readerStop.load()) {
            pollfd pfd{fd, POLLIN, 0};
            const int rc = ::poll(&pfd, 1, 200);
            if (rc < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }
            if (rc == 0)
                continue;
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))
                break;
            char chunk[16384];
            const ssize_t n = ::read(fd, chunk, sizeof(chunk));
            if (n <= 0)
                break;
            buffer.append(chunk, static_cast<std::size_t>(n));

            std::size_t offset = 0;
            for (;;) {
                DBusMessage msg;
                std::size_t used = 0;
                String error;
                if (!DBusMessage::parse(buffer.data() + offset,
                                        buffer.size() - offset, msg, used,
                                        &error)) {
                    if (!error.isEmpty())
                        stopping.store(true); // Malformed data: drop it.
                    break;
                }
                offset += used;
                dispatch(msg);
            }
            buffer.erase(0, offset);
        }
        if (readerStop.load() && !stopping.load())
            return; // SocketNotifier owns the descriptor from now on.
        // Unblock all pending calls.
        std::lock_guard<std::mutex> lk(pendingMutex);
        for (auto& [serial, pending] : pendingCalls) {
            pending->error = "connection lost";
            pending->done = true;
        }
        pendingCv.notify_all();
    }

    void dispatch(const DBusMessage& msg) {
        switch (msg.type()) {
        case DBusMessage::Type::MethodReturn:
        case DBusMessage::Type::Error: {
            AsyncCall async;
            {
                std::lock_guard<std::mutex> lk(pendingMutex);
                auto it = pendingCalls.find(msg.replySerial());
                if (it != pendingCalls.end()) {
                    it->second->reply = msg;
                    it->second->done = true;
                }
                auto asyncIt = asyncCalls.find(msg.replySerial());
                if (asyncIt != asyncCalls.end()) {
                    async = asyncIt->second;
                    asyncCalls.erase(asyncIt);
                }
            }
            pendingCv.notify_all();
            if (async.message)
                finishAsync(async, msg);
            break;
        }
        case DBusMessage::Type::MethodCall:
            handleMethodCall(msg);
            break;
        case DBusMessage::Type::Signal: {
            std::vector<SignalHandler> handlers;
            {
                std::lock_guard<std::mutex> lk(signalsMutex);
                for (const auto& sub : subscriptions) {
                    const bool interfaceMatch =
                        sub.interface.isEmpty() ||
                        sub.interface == msg.interface();
                    const bool memberMatch = sub.member.isEmpty() ||
                                             sub.member == msg.member();
                    if (interfaceMatch && memberMatch)
                        handlers.push_back(sub.handler);
                }
            }
            for (const auto& handler : handlers)
                handler(msg);
            break;
        }
        default:
            break;
        }
    }

    void handleMethodCall(const DBusMessage& msg) {
        MethodHandler handler;
        bool found = false;
        {
            std::lock_guard<std::mutex> lk(objectsMutex);
            auto it = objects.find(msg.path().toStdString());
            if (it != objects.end()) {
                handler = it->second.handler;
                found = true;
            }
        }
        if (found &&
            msg.interface() == "org.freedesktop.DBus.Introspectable" &&
            msg.member() == "Introspect") {
            DBusMessage reply = DBusMessage::createMethodReturn(msg);
            reply.setArguments({DBusValue(introspectXmlFor(msg.path()))});
            sendMessage(reply);
            return;
        }
        if (!handler) {
            sendMessage(DBusMessage::createError(
                msg, "org.freedesktop.DBus.Error.UnknownObject",
                String("no object at path ") + msg.path()));
            return;
        }
        DBusMessage reply;
        try {
            reply = handler(msg);
        } catch (const DBusError& e) {
            reply = DBusMessage::createError(msg, e.name(), e.message());
        } catch (const std::exception& e) {
            reply = DBusMessage::createError(
                msg, "org.freedesktop.DBus.Error.Failed", String(e.what()));
        }
        if (reply.type() == DBusMessage::Type::Invalid)
            reply = DBusMessage::createMethodReturn(msg);
        sendMessage(reply);
    }

    bool sendMessage(DBusMessage msg) {
        msg.setSerial(nextSerial++);
        const ByteArray wire = msg.serialize();
        return writeAll(wire.constData(),
                        static_cast<std::size_t>(wire.size()));
    }

    String introspectXmlFor(const String& path) const {
        std::lock_guard<std::mutex> lk(objectsMutex);
        auto it = objects.find(path.toStdString());
        if (it == objects.end())
            return String();
        return String(it->second.xml);
    }

    struct RegisteredObject {
        std::vector<DBusInterfaceInfo> interfaces;
        std::string xml;
        MethodHandler handler;
    };

    struct Subscription {
        String interface;
        String member;
        SignalHandler handler;
    };

    DBusConnection* owner = nullptr;
    int fd = -1;
    bool connected = false;
    bool eventDriven = false;
    String uniqueName;
    std::atomic<bool> stopping{false};
    std::atomic<bool> readerStop{false};
    std::atomic<std::uint32_t> nextSerial{1};
    std::thread reader;
    std::unique_ptr<SocketNotifier> notifier;
    std::string readBuffer;
    std::mutex writeMutex;
    std::mutex pendingMutex;
    std::condition_variable pendingCv;
    std::map<std::uint32_t, std::shared_ptr<PendingCall>> pendingCalls;
    std::map<std::uint32_t, AsyncCall> asyncCalls;
    std::mutex signalsMutex;
    std::vector<Subscription> subscriptions;
    mutable std::mutex objectsMutex;
    std::map<std::string, RegisteredObject> objects;
};

// ---------------------------------------------------------------------------
// DBusConnection public API
// ---------------------------------------------------------------------------

DBusConnection::DBusConnection(BusType bus, Object* parent)
    : Object(parent), d(std::make_unique<Impl>(bus, this)) {}

DBusConnection::~DBusConnection() = default;

bool DBusConnection::isConnected() const { return d->connected; }
String DBusConnection::uniqueName() const { return d->uniqueName; }

DBusMessage DBusConnection::call(const DBusMessage& message, int timeoutMs) {
    return d->call(message, timeoutMs);
}

DBusMessage* DBusConnection::asyncCall(const DBusMessage& message,
                                       int timeoutMs) {
    return d->asyncCall(message, timeoutMs);
}

bool DBusConnection::send(const DBusMessage& message) {
    return d->sendMessage(message);
}

bool DBusConnection::requestName(const String& name) {
    DBusMessage request = DBusMessage::createMethodCall(
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "RequestName");
    request.setArguments({DBusValue(name), DBusValue(std::uint32_t(0))});
    const DBusMessage reply = call(request);
    // DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER == 1
    return !reply.arguments().empty() &&
           reply.arguments().front().toUInt32() == 1;
}

bool DBusConnection::addMatch(const String& rule) {
    DBusMessage request = DBusMessage::createMethodCall(
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "AddMatch");
    request.setArguments({DBusValue(rule)});
    call(request);
    return true;
}

void DBusConnection::subscribeSignal(const String& interface,
                                     const String& member,
                                     SignalHandler handler) {
    std::lock_guard<std::mutex> lk(d->signalsMutex);
    d->subscriptions.push_back({interface, member, std::move(handler)});
}

void DBusConnection::registerObject(
    const String& path, std::vector<DBusInterfaceInfo> interfaces,
    MethodHandler handler) {
    Impl::RegisteredObject object;
    object.xml = buildIntrospectionXml(interfaces).toStdString();
    object.interfaces = std::move(interfaces);
    object.handler = std::move(handler);
    std::lock_guard<std::mutex> lk(d->objectsMutex);
    d->objects[path.toStdString()] = std::move(object);
}

String DBusConnection::introspectXml(const String& path) const {
    return d->introspectXmlFor(path);
}

bool DBusConnection::emitDBusSignal(const String& path,
                                    const String& interface,
                                    const String& name,
                                    std::vector<DBusValue> args) {
    DBusMessage signal = DBusMessage::createSignal(path, interface, name);
    signal.setArguments(std::move(args));
    return send(signal);
}

} // namespace Firmir

