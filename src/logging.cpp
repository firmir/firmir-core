/**
 * SPDX-FileCopyrightText: 2026 Mikhail Firsov <firsov62121@gmail.com>
 * SPDX-License-Identifier: MIT
 */

#include "logging.h"

#include <optional>
#include <cstdlib>

namespace Firmir {

Logger::Logger(Category category)
    : m_category(category)
{
    if (!enabled())
        return;

    switch (category) {
    case Category::Debug:
        std::cout << "Debug: ";
        break;
    case Category::Warning:
        std::cout << "\033[1m" << "Warning: "; // bold
        break;
    case Category::Error:
        std::cerr << "\033[1m\033[31m" << "Error: "; // red and bold
        break;
    default:
        break;
    }
}

Logger::~Logger()
{
    if (!enabled())
        return;

    auto &out = m_category == Category::Error ? std::cerr : std::cout;
    out << "\033[0m" << '\n' << std::flush;
}

bool Logger::enabled()
{
    if (m_category != Category::Debug)
        return true;

    static std::optional<bool> debugEnabled;
    if (!debugEnabled) {
        debugEnabled = false;
        const char *logLevel = std::getenv("LOG_LEVEL");
        if (logLevel && std::string(logLevel) == "Debug")
            debugEnabled = true;
    }
    return *debugEnabled;
}

Debug::Debug()
    : Logger(Category::Debug)
{
}

Warning::Warning()
    : Logger(Category::Warning)
{
}

Error::Error()
    : Logger(Category::Error)
{
}

Info::Info()
    : Logger(Category::Info)
{
}

} // namespace Firmir
