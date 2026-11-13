// Core example: the Firmir value types working together.
//
// Part 1 showcases the value classes that do not touch the file system:
// String, ByteArray, Variant, DataStream, TextStream (over a String)
// and Future/Concurrent. Part 2 is the classic Object + Thread + Timer
// demo: a Worker moved into a dedicated Thread computes a sum and reports
// back through queued signals, with a watchdog Timer as a safety net.
//
// Debug output is off by default; the example turns it on for the
// "app.worker" category via Log::setLoggingRules() (the in-code analogue of
// the FIRMIR_LOGGING_RULES environment variable).

#include <firmir/bytearray.hpp>
#include <firmir/datastream.hpp>
#include <firmir/future.hpp>
#include <firmir/log.hpp>
#include <firmir/object.hpp>
#include <firmir/string.hpp>
#include <firmir/textstream.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>
#include <firmir/variant.hpp>

#include <chrono>
#include <string>

namespace {

const Firmir::LoggingCategory& lcApp() {
    static const Firmir::LoggingCategory category("app");
    return category;
}

const Firmir::LoggingCategory& lcWorker() {
    static const Firmir::LoggingCategory category("app.worker");
    return category;
}

// ---------------------------------------------------------------------------
// Part 1: value types
// ---------------------------------------------------------------------------

void showcaseValueTypes() {
    using namespace Firmir;

    // String: string API over UTF-8 bytes.
    const String greeting = String("%1, %2!").arg("hello").arg("firmir");
    cLogInfo(lcApp()) << "String: " << greeting
                      << (" (upper: " + greeting.toUpper() + ")");

    // ByteArray: binary-safe container with base64/hex helpers.
    const ByteArray binary(std::string("firmir\0data", 12));
    cLogInfo(lcApp()) << "ByteArray: " << binary.size() << " bytes, base64 "
                      << binary.toBase64().toStdString();

    // Variant: type-tagged union with lenient conversions.
    bool ok = false;
    const Variant answer = 42;
    const Variant text = "3.5";
    cLogInfo(lcApp()) << "Variant: " << answer.toString() << " + "
                      << text.toDouble(&ok) << (ok ? "" : " (failed)");

    // DataStream: binary serialization in a portable wire format.
    ByteArray buffer;
    DataStream out(&buffer);
    out << std::int32_t(7) << String("payload") << 2.5;
    DataStream in(buffer);
    std::int32_t number = 0;
    String label;
    double ratio = 0.0;
    in >> number >> label >> ratio;
    cLogInfo(lcApp()) << "DataStream: " << number << (" '" + label + "' ")
                      << ratio
                      << (" (" + std::to_string(buffer.size()) +
                          " bytes on the wire)");

    // TextStream over a String: formatted text assembly and parsing.
    String textDoc;
    {
        TextStream ts(&textDoc);
        ts << "sum " << 5050 << '\n' << "pi " << 3.14159;
    }
    String word;
    String numberWord;
    TextStream ts(&textDoc);
    ts >> word >> numberWord;
    const int value = numberWord.toInt();
    cLogInfo(lcApp()) << "TextStream: read" << (" '" + word + "'") << value
                      << " from:" << textDoc;

    // Future + Concurrent::run: QtConcurrent::run analogue.
    Future<int> future = Concurrent::run([] {
        int sum = 0;
        for (int i = 1; i <= 100; ++i)
            sum += i;
        return sum;
    });
    cLogInfo(lcApp()) << "Future: concurrent sum is " << future.result();
}

// ---------------------------------------------------------------------------
// Part 2: Object + Thread + Timer
// ---------------------------------------------------------------------------

class Worker : public Firmir::Object {
public:
    using Firmir::Object::Object;

    void doWork() {
        Firmir::cLogDebug(lcWorker()) << "starting computation";
        int result = 0;
        for (int i = 1; i <= 100; ++i)
            result += i; // Simulated heavy computation.
        Firmir::cLogDebug(lcWorker())
            << "computation finished, emitting resultReady";
        emitSignal("resultReady", result);
    }
};

} // namespace

int main() {
    using namespace std::chrono_literals;

    // QT_LOGGING_RULES analogue: enable Debug only for the worker category.
    Firmir::Log::setLoggingRules("app.worker.debug=true");

    Firmir::cLogInfo(lcApp()) << "--- value types ---";
    showcaseValueTypes();

    Firmir::cLogInfo(lcApp()) << "--- threads ---";
    Firmir::cLogInfo(lcApp()) << "starting worker thread";

    Firmir::Thread workerThread;
    Worker worker;
    worker.moveToThread(&workerThread);

    Firmir::Object mainContext;

    // Queued kick-off: emitted in main, delivered in the worker thread.
    worker.connect(&worker, "start", [&worker] { worker.doWork(); },
                   Firmir::ConnectionMode::Queued);

    // Result travels back to the main thread (Auto resolves to Queued here).
    worker.connect<int>(&mainContext, "resultReady",
                   [&](int value) {
                       Firmir::cLogInfo(lcApp())
                           << "result from worker thread: " << value;
                       workerThread.quit();
                       Firmir::Thread::mainThread()->quit();
                   },
                   Firmir::ConnectionMode::Auto);

    // Watchdog: aborts the demo if the worker never replies.
    Firmir::Timer watchdog;
    watchdog.setSingleShot(true);
    watchdog.setInterval(900ms);
    watchdog.connect(&mainContext, "timeout",
                     [] {
                         Firmir::cLogError(lcApp())
                             << "watchdog expired, aborting";
                         Firmir::Thread::mainThread()->exit(1);
                     },
                     Firmir::ConnectionMode::Direct);

    workerThread.start();
    watchdog.start();
    worker.emitSignal("start");

    const int code = Firmir::Thread::mainThread()->exec();
    workerThread.wait();
    Firmir::cLogInfo(lcApp()) << "done, exit code " << code;
    return code;
}
