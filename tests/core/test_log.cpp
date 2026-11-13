#include "test_framework.hpp"

#include <firmir/log.hpp>

#include <iostream>
#include <string>
#include <tuple>
#include <vector>

using namespace Firmir;

namespace {

using Captured = std::vector<std::tuple<LogLevel, std::string, std::string>>;

struct SinkGuard {
    Captured messages;

    SinkGuard() {
        Log::setMinimumLevel(LogLevel::Debug);
        // Debug is off by default since the logging-rules feature; the
        // pre-existing tests assume it is on, so enable it via a rule.
        Log::setLoggingRules("default.debug=true;net.debug=true;factory.debug=true");
        Log::setSink([this](LogLevel level, const std::string& category,
                            const std::string& message) {
            messages.emplace_back(level, category, message);
        });
    }
    ~SinkGuard() {
        Log::setSink(nullptr);
        Log::setMinimumLevel(LogLevel::Debug);
        Log::setLoggingRules("");
    }

    const std::string& text(std::size_t i) const {
        return std::get<2>(messages.at(i));
    }
    LogLevel level(std::size_t i) const { return std::get<0>(messages.at(i)); }
    const std::string& category(std::size_t i) const {
        return std::get<1>(messages.at(i));
    }
};

} // namespace

TEST_CASE(log_basic_streaming) {
    SinkGuard sink;

    logDebug() << "value:" << 42 << " ok";
    CHECK_EQ(sink.messages.size(), 1u);
    CHECK_EQ(sink.text(0), "value: 42 ok");
    CHECK(sink.level(0) == LogLevel::Debug);
    CHECK_EQ(sink.category(0), "default");

    Log(LogLevel::Info) << "explicit " << 3.5;
    CHECK_EQ(sink.messages.size(), 2u);
    CHECK_EQ(sink.text(1), "explicit 3.5");
    CHECK(sink.level(1) == LogLevel::Info);
}

TEST_CASE(log_auto_spacing) {
    SinkGuard sink;

    // Adjacent values are separated by one space automatically.
    logInfo() << "loaded" << "display" << "backend" << "/tmp/x";
    CHECK_EQ(sink.messages.size(), 1u);
    CHECK_EQ(sink.text(0), "loaded display backend /tmp/x");

    // Numbers and empty chunks: empty chunks do not produce a separator.
    logInfo() << "items" << 3 << "" << "left";
    CHECK_EQ(sink.text(1), "items 3 left");

    // Whitespace already present on either side is never duplicated.
    logInfo() << "trailing " << 1;
    logInfo() << "leading" << " 2";
    logInfo() << "tab" << "\t" << "x";
    CHECK_EQ(sink.text(2), "trailing 1");
    CHECK_EQ(sink.text(3), "leading 2");
    CHECK_EQ(sink.text(4), "tab\tx");

    // std::endl is a chunk starting with a newline: no space is inserted.
    logInfo() << "line" << std::endl;
    CHECK_EQ(sink.text(5), "line\n");
}

TEST_CASE(log_global_minimum_level) {
    SinkGuard sink;
    Log::setLoggingRules(""); // The explicit global floor beats the rules.

    Log::setMinimumLevel(LogLevel::Warning);
    logDebug() << "suppressed";
    logInfo() << "also suppressed";
    CHECK(sink.messages.empty());

    logWarning() << "shown";
    logError() << "shown too";
    CHECK_EQ(sink.messages.size(), 2u);
    CHECK_EQ(sink.text(0), "shown");
    CHECK(sink.level(1) == LogLevel::Error);
}

TEST_CASE(log_categories) {
    SinkGuard sink;

    const LoggingCategory net("net");
    CHECK_EQ(net.name(), "net");

    cLogDebug(net) << "connected";
    cLog(net, LogLevel::Info) << "bytes:" << 128;
    CHECK_EQ(sink.messages.size(), 2u);
    CHECK_EQ(sink.category(0), "net");
    CHECK_EQ(sink.text(1), "bytes: 128");

    LoggingCategory noisy("noisy");
    noisy.setMinimumLevel(LogLevel::Error);
    CHECK(!noisy.isEnabled(LogLevel::Warning));
    CHECK(noisy.isEnabled(LogLevel::Error));
    cLogWarning(noisy) << "filtered out";
    cLogError(noisy) << "kept";
    CHECK_EQ(sink.messages.size(), 3u);
    CHECK_EQ(sink.text(2), "kept");
    CHECK_EQ(sink.category(2), "noisy");

    // Copies share the same configuration.
    const LoggingCategory copy = noisy;
    cLogDebug(copy) << "also filtered";
    CHECK_EQ(sink.messages.size(), 3u);
}

TEST_CASE(log_convenience_factories) {
    SinkGuard sink;

    const LoggingCategory cat("factory");
    cLogDebug(cat) << "d";
    cLogInfo(cat) << "i";
    cLogWarning(cat) << "w";
    cLogError(cat) << "e";
    CHECK_EQ(sink.messages.size(), 4u);
    CHECK(sink.level(0) == LogLevel::Debug);
    CHECK(sink.level(1) == LogLevel::Info);
    CHECK(sink.level(2) == LogLevel::Warning);
    CHECK(sink.level(3) == LogLevel::Error);

    CHECK_EQ(std::string(Log::levelName(LogLevel::Debug)), "DEBUG");
    CHECK_EQ(std::string(Log::levelName(LogLevel::Info)), "INFO");
    CHECK_EQ(std::string(Log::levelName(LogLevel::Warning)), "WARN");
    CHECK_EQ(std::string(Log::levelName(LogLevel::Error)), "ERROR");
}

TEST_CASE(log_debug_off_by_default) {
    SinkGuard sink;
    Log::setLoggingRules(""); // Back to defaults.

    const LoggingCategory net("defaults.net");
    CHECK(!net.isEnabled(LogLevel::Debug));
    CHECK(net.isEnabled(LogLevel::Info));

    cLogDebug(net) << "hidden";
    cLogInfo(net) << "shown";
    logDebug() << "hidden too";
    CHECK_EQ(sink.messages.size(), 1u);
    CHECK_EQ(sink.text(0), "shown");
}

TEST_CASE(log_rules_wildcard) {
    SinkGuard sink;
    Log::setLoggingRules("net.*.debug=true");

    const LoggingCategory core("net.core");
    const LoggingCategory app("app");
    CHECK(core.isEnabled(LogLevel::Debug));
    CHECK(!app.isEnabled(LogLevel::Debug));

    cLogDebug(core) << "visible";
    cLogDebug(app) << "hidden";
    CHECK_EQ(sink.messages.size(), 1u);
    CHECK_EQ(sink.text(0), "visible");
}

TEST_CASE(log_rules_last_wins_and_levels) {
    SinkGuard sink;
    Log::setLoggingRules("*.debug=true;net.core.debug=false;net.core.warning=false");

    const LoggingCategory core("net.core");
    const LoggingCategory io("net.io");
    CHECK(!core.isEnabled(LogLevel::Debug));
    CHECK(!core.isEnabled(LogLevel::Warning));
    CHECK(core.isEnabled(LogLevel::Error)); // Error not mentioned: default on.
    CHECK(io.isEnabled(LogLevel::Debug));   // Only net.core was disabled.

    cLogWarning(core) << "suppressed";
    cLogError(core) << "kept";
    CHECK_EQ(sink.messages.size(), 1u);
    CHECK_EQ(sink.text(0), "kept");
}

TEST_CASE(log_rules_regexp) {
    SinkGuard sink;
    // RegExp syntax (anchors, groups): enable debug for "bus.<word>" only.
    Log::setLoggingRules("^bus\\.[a-z]+$.debug=true");

    const LoggingCategory busNet("bus.net");
    const LoggingCategory busDeep("bus.net.deep");
    CHECK(busNet.isEnabled(LogLevel::Debug));
    CHECK(!busDeep.isEnabled(LogLevel::Debug));

    // Equivalent written directly (level suffix after the anchored pattern).
    Log::setLoggingRules("^app\\.(ui|io)$.debug=true");
    const LoggingCategory appUi("app.ui");
    const LoggingCategory appNet("app.net");
    CHECK(appUi.isEnabled(LogLevel::Debug));
    CHECK(!appNet.isEnabled(LogLevel::Debug));
}

TEST_CASE(log_rules_apply_to_default_category) {
    SinkGuard sink;
    Log::setLoggingRules("default.debug=false");

    logDebug() << "hidden";
    logInfo() << "shown";
    CHECK_EQ(sink.messages.size(), 1u);
    CHECK_EQ(sink.text(0), "shown");
    CHECK_EQ(Log::loggingRules(), "default.debug=false");
}

TEST_CASE(log_moved_message_flushes_once) {
    SinkGuard sink;

    {
        Log a(LogLevel::Info);
        a << "payload";
        Log b(std::move(a));
        b << " continued";
    } // b flushes here; moved-from a must not flush anything.
    CHECK_EQ(sink.messages.size(), 1u);
    CHECK_EQ(sink.text(0), "payload continued");
}
