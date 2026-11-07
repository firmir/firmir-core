#include "test_framework.hpp"

#include <firmir/dbuscodegen.hpp>
#include <firmir/dbusconnection.hpp>
#include <firmir/dbusmessage.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

// Generated at build time by firmir-dbus-tool (see tests/CMakeLists.txt).
#include "generated/calculator_adaptor.hpp"
#include "generated/calculator_proxy.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <signal.h>

using namespace Firmir;

namespace {

DBusMessage roundTrip(const DBusMessage& msg) {
    const ByteArray wire = msg.serialize();
    DBusMessage parsed;
    std::size_t consumed = 0;
    String error;
    const bool ok = DBusMessage::parse(wire.constData(),
                                       static_cast<std::size_t>(wire.size()),
                                       parsed, consumed, &error);
    CHECK(ok);
    CHECK_EQ(consumed, static_cast<std::size_t>(wire.size()));
    CHECK(error.isEmpty());
    return parsed;
}

std::vector<DBusValue> sampleArgs() {
    std::vector<DBusValue> args;
    args.push_back(DBusValue(std::uint8_t(7)));
    args.push_back(DBusValue(true));
    args.push_back(DBusValue(std::int16_t(-3)));
    args.push_back(DBusValue(std::uint16_t(4)));
    args.push_back(DBusValue(std::int32_t(-42)));
    args.push_back(DBusValue(std::uint32_t(42)));
    args.push_back(DBusValue(std::int64_t(-1234567890123LL)));
    args.push_back(DBusValue(std::uint64_t(1234567890123ULL)));
    args.push_back(DBusValue(2.5));
    args.push_back(DBusValue(String("hello")));
    args.push_back(DBusValue::objectPath("/org/firmir/Test"));
    args.push_back(DBusValue::signature("(ia{sv})"));
    args.push_back(DBusValue::makeArray(
        "i", {DBusValue(std::int32_t(1)), DBusValue(std::int32_t(2)),
              DBusValue(std::int32_t(3))}));
    args.push_back(DBusValue::makeStruct(
        {DBusValue(String("pair")), DBusValue(std::int32_t(9))}));
    args.push_back(DBusValue::makeDict(
        "s", "i",
        {{DBusValue(String("one")), DBusValue(std::int32_t(1))},
         {DBusValue(String("two")), DBusValue(std::int32_t(2))}}));
    args.push_back(DBusValue::makeVariant(DBusValue(String("boxed"))));
    args.push_back(DBusValue::makeArray(
        "(si)",
        {DBusValue::makeStruct(
             {DBusValue(String("a")), DBusValue(std::int32_t(1))}),
         DBusValue::makeStruct(
             {DBusValue(String("b")), DBusValue(std::int32_t(2))})}));
    return args;
}

} // namespace

TEST_CASE(dbus_value_signatures) {
    CHECK_EQ(DBusValue(std::int32_t(1)).signatureString(), "i");
    CHECK_EQ(DBusValue(String("x")).signatureString(), "s");
    CHECK_EQ(DBusValue::objectPath("/a").signatureString(), "o");
    CHECK_EQ(DBusValue::signature("i").signatureString(), "g");
    CHECK_EQ(DBusValue::makeArray("s", {}).signatureString(), "as");
    CHECK_EQ(DBusValue::makeStruct({DBusValue(std::int32_t(1)),
                                    DBusValue(String("x"))})
                 .signatureString(),
             "(is)");
    CHECK_EQ(DBusValue::makeDict("s", "i", {}).signatureString(), "a{si}");
    CHECK_EQ(DBusValue::makeVariant(DBusValue(true)).signatureString(), "v");
}

TEST_CASE(dbus_message_roundtrip_all_types) {
    DBusMessage call = DBusMessage::createMethodCall(
        "org.firmir.Service", "/org/firmir/Test", "org.firmir.Test", "DoAll");
    call.setSerial(77);
    call.setArguments(sampleArgs());

    const DBusMessage parsed = roundTrip(call);
    CHECK(parsed.type() == DBusMessage::Type::MethodCall);
    CHECK_EQ(parsed.destination(), "org.firmir.Service");
    CHECK_EQ(parsed.path(), "/org/firmir/Test");
    CHECK_EQ(parsed.interface(), "org.firmir.Test");
    CHECK_EQ(parsed.member(), "DoAll");
    CHECK_EQ(parsed.serial(), 77u);
    CHECK_EQ(parsed.argumentsSignature(), "ybnqiuxtdsogai(si)a{si}va(si)");
    CHECK(parsed.arguments() == call.arguments());
}

TEST_CASE(dbus_message_reply_and_error) {
    DBusMessage call = DBusMessage::createMethodCall("d", "/p", "i", "M");
    call.setSerial(5);
    call.setSender(":1.99");

    DBusMessage reply = DBusMessage::createMethodReturn(call);
    reply.setSerial(6);
    reply.setArguments({DBusValue(std::int32_t(200))});
    const DBusMessage parsedReply = roundTrip(reply);
    CHECK(parsedReply.type() == DBusMessage::Type::MethodReturn);
    CHECK_EQ(parsedReply.replySerial(), 5u);
    CHECK_EQ(parsedReply.destination(), ":1.99");
    CHECK_EQ(parsedReply.arguments().front().toInt32(), 200);

    DBusMessage error =
        DBusMessage::createError(call, "org.firmir.Broken", "it broke");
    error.setSerial(7);
    const DBusMessage parsedError = roundTrip(error);
    CHECK(parsedError.type() == DBusMessage::Type::Error);
    CHECK_EQ(parsedError.errorName(), "org.firmir.Broken");
    CHECK_EQ(parsedError.replySerial(), 5u);
    const DBusError ex = dbusErrorFromMessage(parsedError);
    CHECK_EQ(ex.name(), "org.firmir.Broken");
    CHECK_EQ(ex.message(), "it broke");
}

TEST_CASE(dbus_message_partial_parse) {
    DBusMessage call = DBusMessage::createMethodCall("d", "/p", "i", "M");
    call.setSerial(1);
    call.setArguments({DBusValue(String("payload"))});
    const ByteArray wire = call.serialize();

    // Every proper prefix must report "incomplete", never crash.
    for (int size = 0; size < wire.size(); ++size) {
        DBusMessage parsed;
        std::size_t consumed = 77;
        String error;
        CHECK(!DBusMessage::parse(wire.constData(),
                                  static_cast<std::size_t>(size), parsed,
                                  consumed, &error));
        CHECK_EQ(consumed, 0u);
    }
    DBusMessage parsed;
    std::size_t consumed = 0;
    CHECK(DBusMessage::parse(wire.constData(),
                             static_cast<std::size_t>(wire.size()), parsed,
                             consumed));
    CHECK_EQ(consumed, static_cast<std::size_t>(wire.size()));
}

TEST_CASE(dbus_message_malformed) {
    String error;
    DBusMessage parsed;
    std::size_t consumed = 0;

    // Big-endian is not supported.
    char bad[16] = {'B', 1, 0, 1};
    CHECK(!DBusMessage::parse(bad, sizeof(bad), parsed, consumed, &error));
    CHECK(!error.isEmpty());

    error.clear();
    char badType[16] = {'l', 9, 0, 1};
    CHECK(!DBusMessage::parse(badType, sizeof(badType), parsed, consumed,
                              &error));
    CHECK(!error.isEmpty());
}

// ---------------------------------------------------------------------------
// Code generation
// ---------------------------------------------------------------------------

namespace {

const char* kCalculatorXml = R"xml(
<node>
  <interface name="org.firmir.Calculator">
    <method name="Add">
      <arg name="a" type="i" direction="in"/>
      <arg name="b" type="i" direction="in"/>
      <arg name="result" type="i" direction="out"/>
    </method>
    <method name="Divide">
      <arg name="num" type="i" direction="in"/>
      <arg name="denom" type="i" direction="in"/>
      <arg name="quotient" type="i" direction="out"/>
      <arg name="remainder" type="i" direction="out"/>
    </method>
    <method name="Notify"/>
    <method name="Names">
      <arg name="names" type="as" direction="out"/>
    </method>
    <signal name="Computed">
      <arg name="value" type="i"/>
    </signal>
  </interface>
</node>
)xml";

} // namespace

TEST_CASE(dbus_codegen_parse_xml) {
    String error;
    const std::vector<DBusInterfaceInfo> ifaces =
        parseInterfacesXml(kCalculatorXml, &error);
    CHECK_EQ(ifaces.size(), 1u);
    CHECK(error.isEmpty());

    const DBusInterfaceInfo& iface = ifaces.front();
    CHECK_EQ(iface.name, "org.firmir.Calculator");
    CHECK_EQ(iface.methods.size(), 4u);
    CHECK_EQ(iface.signals.size(), 1u);
    CHECK_EQ(iface.methods[0].name, "Add");
    CHECK_EQ(iface.methods[0].inArgs.size(), 2u);
    CHECK_EQ(iface.methods[0].outArgs.size(), 1u);
    CHECK_EQ(iface.methods[0].inArgs[1].signature, "i");
    CHECK_EQ(iface.methods[1].outArgs.size(), 2u);
    CHECK_EQ(iface.signals[0].name, "Computed");
    CHECK_EQ(iface.signals[0].args[0].signature, "i");
}

TEST_CASE(dbus_codegen_proxy_text) {
    String error;
    const auto ifaces = parseInterfacesXml(kCalculatorXml, &error);
    const String proxy = generateProxy(ifaces.front());
    const std::string code = proxy.toStdString();

    CHECK(code.find("class CalculatorProxy") != std::string::npos);
    CHECK(code.find("std::int32_t Add(std::int32_t a, std::int32_t b)") !=
          std::string::npos);
    CHECK(code.find("void Divide(std::int32_t num, std::int32_t denom, "
                    "std::int32_t& quotient, std::int32_t& remainder)") !=
          std::string::npos);
    CHECK(code.find("void Notify()") != std::string::npos);
    CHECK(code.find("std::vector<Firmir::String> Names()") !=
          std::string::npos);
    CHECK(code.find("void connectComputed("
                    "std::function<void(std::int32_t value)> handler)") !=
          std::string::npos);
    CHECK(code.find("\"org.firmir.Calculator\"") != std::string::npos);
}

TEST_CASE(dbus_codegen_adaptor_text) {
    String error;
    const auto ifaces = parseInterfacesXml(kCalculatorXml, &error);
    const String adaptor = generateAdaptor(ifaces.front());
    const std::string code = adaptor.toStdString();

    CHECK(code.find("class CalculatorAdaptor") != std::string::npos);
    CHECK(code.find("virtual std::int32_t Add(std::int32_t a, "
                    "std::int32_t b) = 0;") != std::string::npos);
    CHECK(code.find("virtual void Divide(std::int32_t num, std::int32_t "
                    "denom, std::int32_t& quotient, std::int32_t& remainder) "
                    "= 0;") != std::string::npos);
    CHECK(code.find("if (call_.member() == \"Add\")") != std::string::npos);
    CHECK(code.find("interfaceInfo()") != std::string::npos);
    CHECK(code.find("info.methods.push_back({\"Add\"") != std::string::npos);
    CHECK(code.find("info.signals.push_back({\"Computed\"") !=
          std::string::npos);
}

// ---------------------------------------------------------------------------
// Integration with a real D-Bus daemon
// ---------------------------------------------------------------------------

namespace {

struct TestBus {
    std::string address;
    pid_t pid = -1;
    bool ok = false;

    TestBus() {
        FILE* fp = ::popen("dbus-daemon --session --print-address=1 "
                           "--print-pid=1 --fork 2>/dev/null",
                           "r");
        if (!fp)
            return;
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
        ok = !address.empty() && pid > 0;
        if (ok)
            ::setenv("DBUS_SESSION_BUS_ADDRESS", address.c_str(), 1);
    }
    ~TestBus() {
        if (pid > 0)
            ::kill(pid, SIGTERM);
    }
};

class CalculatorService : public CalculatorAdaptor {
public:
    std::int32_t Add(std::int32_t a, std::int32_t b) override {
        return a + b;
    }
    void Divide(std::int32_t num, std::int32_t denom, std::int32_t& quotient,
                std::int32_t& remainder) override {
        if (denom == 0)
            throw DBusError("org.firmir.Calculator.Error.DivisionByZero",
                            "division by zero");
        quotient = num / denom;
        remainder = num % denom;
    }
    void Notify() override { notified = true; }
    std::vector<Firmir::String> Names() override { return {"one", "two"}; }

    std::atomic<bool> notified{false};
};

} // namespace

TEST_CASE(dbus_integration_service_client) {
    TestBus bus;
    if (!bus.ok) {
        std::fprintf(stderr, "[ SKIP ] dbus-daemon unavailable\n");
        return;
    }

    DBusConnection server;
    DBusConnection client;
    CHECK(server.isConnected());
    CHECK(client.isConnected());
    CHECK_EQ(server.uniqueName().indexOf(":"), 0);

    CHECK(server.requestName("org.firmir.Calculator"));

    CalculatorService service;
    server.registerObject(
        "/org/firmir/Calculator", {CalculatorService::interfaceInfo()},
        [&service](const DBusMessage& msg) { return service.dispatch(msg); });

    CalculatorProxy proxy(client, "org.firmir.Calculator",
                          "/org/firmir/Calculator");
    CHECK_EQ(proxy.Add(2, 3), 5);

    std::int32_t quotient = 0;
    std::int32_t remainder = 0;
    proxy.Divide(7, 2, quotient, remainder);
    CHECK_EQ(quotient, 3);
    CHECK_EQ(remainder, 1);

    proxy.Notify(); // no reply value; the handler records the call
    CHECK(testing::waitFor([&] { return service.notified.load(); }));

    const std::vector<Firmir::String> names = proxy.Names();
    CHECK_EQ(names.size(), 2u);
    CHECK_EQ(names[0], "one");

    // Division by zero delivers a named DBusError.
    bool caught = false;
    try {
        proxy.Divide(1, 0, quotient, remainder);
    } catch (const DBusError& e) {
        caught = true;
        CHECK_EQ(e.name(), "org.firmir.Calculator.Error.DivisionByZero");
    }
    CHECK(caught);

    // Unknown object path.
    CalculatorProxy missing(client, "org.firmir.Calculator", "/missing");
    caught = false;
    try {
        missing.Add(1, 1);
    } catch (const DBusError& e) {
        caught = true;
        CHECK_EQ(e.name(), "org.freedesktop.DBus.Error.UnknownObject");
    }
    CHECK(caught);

    // Introspection over the wire.
    const DBusMessage intro = client.call(DBusMessage::createMethodCall(
        "org.firmir.Calculator", "/org/firmir/Calculator",
        "org.freedesktop.DBus.Introspectable", "Introspect"));
    const String xml = intro.arguments().front().toString();
    CHECK(xml.indexOf("Add") >= 0);
    CHECK(xml.indexOf("Computed") >= 0);

    // Signals: subscribe via the generated proxy, emit from the service.
    client.addMatch("type='signal',interface='org.firmir.Calculator'");
    std::atomic<bool> got{false};
    std::atomic<int> value{0};
    proxy.connectComputed([&](std::int32_t v) {
        value = v;
        got = true;
    });
    server.emitDBusSignal("/org/firmir/Calculator", "org.firmir.Calculator",
                          "Computed", {DBusValue(std::int32_t(64))});
    CHECK(testing::waitFor([&] { return got.load(); }));
    CHECK_EQ(value.load(), 64);

    Object loopContext;
    Timer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(std::chrono::milliseconds(3000));
    watchdog.connect(&loopContext, "timeout",
                     [] { Thread::mainThread()->exit(9); },
                     ConnectionMode::Direct);

    DBusMessage add = DBusMessage::createMethodCall(
        "org.firmir.Calculator", "/org/firmir/Calculator",
        "org.firmir.Calculator", "Add");
    add.setArguments({DBusValue(std::int32_t(20)),
                      DBusValue(std::int32_t(22))});
    DBusMessage* pending = client.asyncCall(add, 2000);
    bool asyncDone = false;
    pending->connect(
        &loopContext, DBusMessage::signalFinished(),
        [&] {
            asyncDone = true;
            CHECK(pending->type() == DBusMessage::Type::MethodReturn);
            CHECK_EQ(pending->arguments().size(), 1u);
            CHECK_EQ(pending->arguments().front().toInt32(), 42);
            Thread::mainThread()->quit();
        },
        ConnectionMode::Direct);
    watchdog.start();
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    watchdog.stop();
    CHECK(asyncDone);

    // Blocking calls keep working after the connection switched to notifier IO.
    CHECK_EQ(proxy.Add(3, 4), 7);

    DBusMessage divide = DBusMessage::createMethodCall(
        "org.firmir.Calculator", "/org/firmir/Calculator",
        "org.firmir.Calculator", "Divide");
    divide.setArguments({DBusValue(std::int32_t(1)),
                         DBusValue(std::int32_t(0))});
    DBusMessage* errorPending = client.asyncCall(divide, 2000);
    bool errorDone = false;
    errorPending->connect(
        &loopContext, DBusMessage::signalFinished(),
        [&] {
            errorDone = true;
            CHECK(errorPending->type() == DBusMessage::Type::Error);
            CHECK_EQ(errorPending->errorName(),
                     "org.firmir.Calculator.Error.DivisionByZero");
            Thread::mainThread()->quit();
        },
        ConnectionMode::Direct);
    watchdog.start();
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    watchdog.stop();
    CHECK(errorDone);

    server.registerObject(
        "/org/firmir/Slow", {},
        [](const DBusMessage& request) {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            return DBusMessage::createMethodReturn(request);
        });
    DBusMessage slow = DBusMessage::createMethodCall(
        "org.firmir.Calculator", "/org/firmir/Slow", "org.firmir.Test", "Slow");
    DBusMessage* timeoutPending = client.asyncCall(slow, 10);
    bool timeoutDone = false;
    timeoutPending->connect(
        &loopContext, DBusMessage::signalFinished(),
        [&] {
            timeoutDone = true;
            CHECK(timeoutPending->type() == DBusMessage::Type::Error);
            CHECK_EQ(timeoutPending->errorName(), "org.firmir.DBus.Error.Timeout");
            Thread::mainThread()->quit();
        },
        ConnectionMode::Direct);
    watchdog.start();
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    watchdog.stop();
    CHECK(timeoutDone);
}
