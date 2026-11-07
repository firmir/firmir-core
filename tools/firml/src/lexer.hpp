#pragma once

#include "error.hpp"

#include <string>

namespace Firmir {
namespace Fml {

enum class TokenKind {
    EndOfFile,
    Identifier,   // [A-Za-z_][A-Za-z0-9_]*
    Number,       // 123, 12.5, 1.5e2, 300ms (unit suffix in Token::suffix)
    String,       // "..." (decoded value in Token::text)
    Hash,         // #
    LBrace,       // {
    RBrace,       // }
    LParen,       // (
    RParen,       // )
    Colon,        // :
    Semicolon,    // ;
    Comma,        // ,
    Dot,          // .
    Question,     // ?
    Plus,         // +
    Minus,        // -
    Star,         // *
    Slash,        // /
    Percent,      // %
    Bang,         // !
    Equal,        // = (using alias)
    EqualEqual,   // ==
    BangEqual,    // !=
    Less,         // <
    LessEqual,    // <=
    Greater,      // >
    GreaterEqual, // >=
    AndAnd,       // &&
    OrOr,         // ||
    IncludePath,  // <...> or "..." immediately after #include
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string text;   // identifier, raw number text, decoded string, include path
    std::string suffix; // Number only: unit suffix (e.g. "ms")
    bool angled = false; // IncludePath only: <...> vs "..."
    SourceLoc loc;
};

/// Hand-written lexer for the Firml markup language (no external tools).
/// Reports failures by throwing Fml::Error with an exact position.
class Lexer {
public:
    /// The source must outlive the lexer.
    Lexer(const std::string& source, std::string fileName);

    /// Returns the next regular token (EndOfFile at the end of input).
    Token next();

    /// Special mode for `#include`: reads <path> or "path" as one token.
    /// Must be called right after the `include` identifier was consumed.
    Token nextIncludePath();

private:
    SourceLoc here() const;
    char peekChar(size_t offset = 0) const;
    char takeChar();
    void skipTrivia(); // whitespace, // line and /* block */ comments
    Token lexIdentifier(SourceLoc start);
    Token lexNumber(SourceLoc start);
    Token lexString(SourceLoc start);
    Token makeToken(TokenKind kind, SourceLoc loc, std::string text = {});

    const std::string& m_source;
    std::string m_file;
    size_t m_pos = 0;
    uint32_t m_line = 1;
    uint32_t m_col = 1;
};

} // namespace Fml
} // namespace Firmir
