#include <firmir/log.hpp>

#include <firmir/regexp.hpp>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

namespace Firmir {

// ---------------------------------------------------------------------------
// Logging rules (QT_LOGGING_RULES analogue)
// ---------------------------------------------------------------------------

namespace {

struct LoggingRule {
    RegExp pattern; // Compiled wildcard or regex; matched against the category.
    int levelMask;  // Bit (1 << int(LogLevel)) for every level the rule covers.
    bool enabled;
};

struct RulesState {
    std::mutex mutex;
    bool initialized = false; // FIRMIR_LOGGING_RULES consumed already?
    std::vector<LoggingRule> rules;
    std::string raw;
};

RulesState& rulesState() {
    static RulesState state;
    return state;
}

// True when the pattern uses RegExp syntax rather than plain wildcards.
bool looksLikeRegex(const std::string& pattern) {
    return pattern.find_first_of("()[]{}+^$|\\") != std::string::npos;
}

std::string wildcardToRegex(const std::string& pattern) {
    std::string out;
    out.reserve(pattern.size() * 2);
    for (const char c : pattern) {
        if (c == '*')
            out += ".*";
        else if (c == '?')
            out += '.';
        else
            out += RegExp::escape(std::string(1, c));
    }
    return out;
}

std::vector<LoggingRule> parseRules(const std::string& text) {
    std::vector<LoggingRule> rules;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t end = text.find_first_of(";\n", pos);
        if (end == std::string::npos)
            end = text.size();
        std::string rule = text.substr(pos, end - pos);
        pos = end + 1;

        // Trim whitespace.
        const std::size_t first = rule.find_first_not_of(" \t\r");
        if (first == std::string::npos)
            continue;
        rule = rule.substr(first, rule.find_last_not_of(" \t\r") - first + 1);

        const std::size_t eq = rule.rfind('=');
        if (eq == std::string::npos || eq + 1 >= rule.size())
            continue;
        std::string value = rule.substr(eq + 1);
        for (char& c : value)
            c = static_cast<char>(std::tolower(c));

        LoggingRule parsed{RegExp(), 0, false};
        parsed.enabled = (value == "true" || value == "1");

        std::string spec = rule.substr(0, eq);
        static const char* kSuffixes[] = {".debug", ".info", ".warning",
                                          ".error"};
        int mask = 0;
        for (int i = 0; i < 4; ++i) {
            const std::string suffix = kSuffixes[i];
            if (spec.size() > suffix.size() &&
                spec.compare(spec.size() - suffix.size(), suffix.size(),
                             suffix) == 0) {
                mask = 1 << i;
                spec.erase(spec.size() - suffix.size());
                break;
            }
        }
        if (mask == 0)
            mask = 0xF; // No level suffix: applies to every level.
        parsed.levelMask = mask;

        parsed.pattern =
            RegExp(looksLikeRegex(spec) ? spec : wildcardToRegex(spec));
        if (!parsed.pattern.isValid())
            continue;
        rules.push_back(std::move(parsed));
    }
    return rules;
}

void ensureRulesInitialized(RulesState& state) {
    // Caller holds state.mutex.
    if (state.initialized)
        return;
    state.initialized = true;
    if (const char* env = std::getenv("FIRMIR_LOGGING_RULES")) {
        state.raw = env;
        state.rules = parseRules(state.raw);
    }
}

/// -1: no rule decides; 0: disabled by a rule; 1: enabled by a rule.
/// Later rules win, mirroring QT_LOGGING_RULES semantics.
int rulesDecision(const std::string& category, LogLevel level) {
    RulesState& state = rulesState();
    std::lock_guard<std::mutex> lock(state.mutex);
    ensureRulesInitialized(state);
    int decision = -1;
    const int bit = 1 << static_cast<int>(level);
    for (auto& rule : state.rules) {
        if ((rule.levelMask & bit) && rule.pattern.exactMatch(category))
            decision = rule.enabled ? 1 : 0;
    }
    return decision;
}

} // namespace

// ---------------------------------------------------------------------------
// LoggingCategory
// ---------------------------------------------------------------------------

struct LoggingCategory::Impl {
    explicit Impl(std::string categoryName) : name(std::move(categoryName)) {}
    std::string name;
    std::atomic<int> minLevel{static_cast<int>(LogLevel::Info)};
};

LoggingCategory::LoggingCategory(const std::string& name)
    : d(std::make_shared<Impl>(name)) {}

LoggingCategory::~LoggingCategory() = default;

LoggingCategory::LoggingCategory(const LoggingCategory&) noexcept = default;
LoggingCategory&
LoggingCategory::operator=(const LoggingCategory&) noexcept = default;

const std::string& LoggingCategory::name() const noexcept { return d->name; }

bool LoggingCategory::isEnabled(LogLevel level) const noexcept {
    bool enabled =
        static_cast<int>(level) >= d->minLevel.load(std::memory_order_relaxed);
    const int decision = rulesDecision(d->name, level);
    if (decision >= 0)
        enabled = decision == 1;
    return enabled;
}

void LoggingCategory::setMinimumLevel(LogLevel level) noexcept {
    d->minLevel.store(static_cast<int>(level), std::memory_order_relaxed);
}

LogLevel LoggingCategory::minimumLevel() const noexcept {
    return static_cast<LogLevel>(d->minLevel.load(std::memory_order_relaxed));
}

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

namespace {

std::mutex g_mutex; // Serializes sink configuration and message delivery.
Log::Sink g_sink;   // null => default stderr sink

LogLevel parseLevelEnv() {
    // The global level is no restriction by default: "Debug off by default"
    // is enforced by the per-category default threshold (Info). An explicit
    // FIRMIR_LOG_LEVEL or setMinimumLevel() acts as a hard floor that even
    // logging rules cannot lower.
    const char* env = std::getenv("FIRMIR_LOG_LEVEL");
    if (!env)
        return LogLevel::Debug;
    std::string value;
    for (const char* p = env; *p; ++p)
        value.push_back(static_cast<char>(std::tolower(*p)));
    if (value == "debug")
        return LogLevel::Debug;
    if (value == "info")
        return LogLevel::Info;
    if (value == "warning")
        return LogLevel::Warning;
    if (value == "error")
        return LogLevel::Error;
    return LogLevel::Debug;
}

std::atomic<int>& globalMinLevel() {
    static std::atomic<int> level{static_cast<int>(parseLevelEnv())};
    return level;
}

void defaultSink(LogLevel level, const std::string& category,
                 const std::string& message) {
    std::fprintf(stderr, "[%s] [%s] %s\n", Log::levelName(level),
                 category.c_str(), message.c_str());
}

} // namespace

// ---------------------------------------------------------------------------
// Log
// ---------------------------------------------------------------------------

struct Log::Impl {
    LogLevel level;
    std::string category;
    bool enabled;
    std::ostringstream stream;
};

Log::Log(LogLevel level)
    : d(std::make_unique<Impl>(
          Impl{level, "default",
               static_cast<int>(level) >=
                   globalMinLevel().load(std::memory_order_relaxed),
               {}})) {
    // The global minimum level is a hard floor; rules only replace the
    // implicit per-category threshold (Info for the "default" category).
    const int decision = rulesDecision("default", level);
    if (decision >= 0)
        d->enabled = d->enabled && decision == 1;
    else
        d->enabled = d->enabled && level >= LogLevel::Info;
}

Log::Log(const LoggingCategory& category, LogLevel level)
    : d(std::make_unique<Impl>(
          Impl{level, category.name(),
               static_cast<int>(level) >=
                       globalMinLevel().load(std::memory_order_relaxed) &&
                   category.isEnabled(level),
               {}})) {}

Log::Log(Log&&) noexcept = default;

Log& Log::operator=(Log&& other) noexcept {
    if (this != &other) {
        flush(); // Deliver a message that was never streamed to completion.
        d = std::move(other.d);
    }
    return *this;
}

Log::~Log() { flush(); }

void Log::flush() {
    if (!d || !d->enabled)
        return;
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_sink)
        g_sink(d->level, d->category, d->stream.str());
    else
        defaultSink(d->level, d->category, d->stream.str());
}

Log& Log::operator<<(std::ostream& (*manip)(std::ostream&)) {
    if (isEnabled()) {
        std::ostringstream oss;
        oss << manip;
        append(oss.str());
    }
    return *this;
}

bool Log::isEnabled() const noexcept { return d && d->enabled; }

void Log::append(const std::string& text) { d->stream << text; }

void Log::setSink(Sink sink) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_sink = std::move(sink);
}

void Log::setMinimumLevel(LogLevel level) noexcept {
    globalMinLevel().store(static_cast<int>(level), std::memory_order_relaxed);
}

LogLevel Log::minimumLevel() noexcept {
    return static_cast<LogLevel>(globalMinLevel().load(std::memory_order_relaxed));
}

void Log::setLoggingRules(const std::string& rules) {
    RulesState& state = rulesState();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.initialized = true; // Explicit rules override FIRMIR_LOGGING_RULES.
    state.raw = rules;
    state.rules = parseRules(rules);
}

std::string Log::loggingRules() {
    RulesState& state = rulesState();
    std::lock_guard<std::mutex> lock(state.mutex);
    ensureRulesInitialized(state);
    return state.raw;
}

const char* Log::levelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    }
    return "?";
}

} // namespace Firmir
