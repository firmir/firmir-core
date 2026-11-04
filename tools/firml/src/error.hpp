#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>

namespace Firmir {
namespace Fml {

/// Position in a .fml source file (1-based line and column).
struct SourceLoc {
    std::string file;
    uint32_t line = 1;
    uint32_t col = 1;
};

/// A single diagnostic with an exact source position. Lexer and parser
/// report every failure by throwing this; main() formats and prints it.
class Error : public std::runtime_error {
public:
    Error(SourceLoc loc, const std::string& message)
        : std::runtime_error(message), m_loc(std::move(loc)) {}

    const SourceLoc& loc() const { return m_loc; }

private:
    SourceLoc m_loc;
};

/// Formats an error as "file:line:col: error: message".
inline std::string formatError(const Error& error) {
    const SourceLoc& loc = error.loc();
    return loc.file + ":" + std::to_string(loc.line) + ":" +
           std::to_string(loc.col) + ": error: " + error.what();
}

/// Formats a diagnostic as "file:line:col: warning: message".
inline std::string formatWarning(const SourceLoc& loc, const std::string& message) {
    return loc.file + ":" + std::to_string(loc.line) + ":" +
           std::to_string(loc.col) + ": warning: " + message;
}

} // namespace Fml
} // namespace Firmir
