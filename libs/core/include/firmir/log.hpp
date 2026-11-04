#pragma once

// Firmir::Log / Firmir::LoggingCategory — stream-style logging inspired by
// qDebug()/qCDebug(). A Log object accumulates one message; the message is
// flushed (with level and category prefix) when the Log is destroyed, i.e. at
// the end of the full expression:
//
//   Firmir::Log(Firmir::LogLevel::Info) << "value:" << 42;
//   Firmir::logWarning() << "something looks off";
//
//   static const Firmir::LoggingCategory lcNet("net");
//   Firmir::cLog(lcNet, Firmir::LogLevel::Debug) << "connected";
//   Firmir::cLogError(lcNet) << "connection lost";
//
// The global minimum level is controlled by Log::setMinimumLevel() and, at
// startup, by the FIRMIR_LOG_LEVEL environment variable
// (debug|info|warning|error); when set, it acts as a hard floor that logging
// rules cannot lower. Each LoggingCategory additionally has its own minimum
// level (default: Info), so Debug messages are off by default and can be
// switched on per category through logging rules.
//
// Per-category filtering follows QT_LOGGING_RULES: the FIRMIR_LOGGING_RULES
// environment variable (or Log::setLoggingRules()) holds rules of the form
//
//   <pattern>[.debug|.info|.warning|.error]=true|false
//
// separated by ';' or newlines. <pattern> is either a wildcard ('*' matches
// any run of characters, '?' a single one) or - when it contains RegExp
// metacharacters such as ()[]{}+^$|\ - an ECMAScript regular expression
// matched against the whole category name. A rule without a level suffix
// applies to all levels; later rules win over earlier ones.
//
// Output goes through a replaceable sink (stderr by default); flushing is
// serialized with a mutex, so logging from multiple threads never
// interleaves within a single message.

#include <functional>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <utility>

namespace Firmir {

/// Severity of a log message, ordered from most to least verbose.
enum class LogLevel { Debug = 0, Info = 1, Warning = 2, Error = 3 };

/// A named logging category, the analogue of QLoggingCategory. Copies share
/// the same configuration, so a category is typically declared once:
///   static const Firmir::LoggingCategory lcNet("net");
class LoggingCategory {
public:
    explicit LoggingCategory(const std::string& name);
    ~LoggingCategory();

    LoggingCategory(const LoggingCategory&) noexcept;
    LoggingCategory& operator=(const LoggingCategory&) noexcept;

    const std::string& name() const noexcept;

    /// True if messages of @p level pass this category's own threshold and
    /// the active logging rules. The global Log::minimumLevel() applies on
    /// top of that.
    bool isEnabled(LogLevel level) const noexcept;

    /// Sets this category's own minimum level (default: Info, i.e. Debug is
    /// off unless re-enabled by logging rules).
    void setMinimumLevel(LogLevel level) noexcept;
    LogLevel minimumLevel() const noexcept;

private:
    struct Impl;
    std::shared_ptr<Impl> d;
};

/// A single-message stream logger. Not copyable; movable. The message is
/// delivered to the sink from the destructor.
class Log {
public:
    /// Uncategorized message; passes if level >= the global minimum level.
    explicit Log(LogLevel level);
    /// Categorized message; passes if enabled by both the category and the
    /// global minimum level.
    Log(const LoggingCategory& category, LogLevel level);

    Log(Log&& other) noexcept;
    Log& operator=(Log&& other) noexcept;
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    /// Flushes the composed message to the sink (unless filtered out).
    ~Log();

    template <typename T>
    Log& operator<<(T&& value) {
        if (isEnabled()) {
            std::ostringstream oss;
            oss << std::forward<T>(value);
            append(oss.str());
        }
        return *this;
    }
    /// Support for stream manipulators such as std::endl or std::hex.
    Log& operator<<(std::ostream& (*manip)(std::ostream&));

    /// Sink receiving (level, category, message) for every flushed message.
    /// nullptr restores the default stderr sink. Called under a global mutex.
    using Sink =
        std::function<void(LogLevel, const std::string&, const std::string&)>;
    static void setSink(Sink sink);

    /// Global minimum level; initialized from FIRMIR_LOG_LEVEL.
    static void setMinimumLevel(LogLevel level) noexcept;
    static LogLevel minimumLevel() noexcept;

    /// Installs category filtering rules (QT_LOGGING_RULES syntax; see the
    /// file-level comment). Overrides FIRMIR_LOGGING_RULES. An empty string
    /// clears all rules. Takes effect immediately for all categories,
    /// including already-created ones.
    static void setLoggingRules(const std::string& rules);
    /// The currently installed rules text (empty when none).
    static std::string loggingRules();

    /// "DEBUG" | "INFO" | "WARN" | "ERROR".
    static const char* levelName(LogLevel level) noexcept;

private:
    bool isEnabled() const noexcept;
    void append(const std::string& text);
    void flush();

    struct Impl;
    std::unique_ptr<Impl> d;
};

// ---------------------------------------------------------------------------
// Convenience factories (qDebug()/qCDebug() style).
// ---------------------------------------------------------------------------

inline Log logDebug() { return Log(LogLevel::Debug); }
inline Log logInfo() { return Log(LogLevel::Info); }
inline Log logWarning() { return Log(LogLevel::Warning); }
inline Log logError() { return Log(LogLevel::Error); }

inline Log cLog(const LoggingCategory& category, LogLevel level) {
    return Log(category, level);
}
inline Log cLogDebug(const LoggingCategory& category) {
    return Log(category, LogLevel::Debug);
}
inline Log cLogInfo(const LoggingCategory& category) {
    return Log(category, LogLevel::Info);
}
inline Log cLogWarning(const LoggingCategory& category) {
    return Log(category, LogLevel::Warning);
}
inline Log cLogError(const LoggingCategory& category) {
    return Log(category, LogLevel::Error);
}

} // namespace Firmir
