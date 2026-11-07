#include "lexer.hpp"

namespace Firmir {
namespace Fml {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
bool isAlphaNum(char c) { return isAlpha(c) || isDigit(c); }

} // namespace

Lexer::Lexer(const std::string& source, std::string fileName)
    : m_source(source), m_file(std::move(fileName)) {}

SourceLoc Lexer::here() const { return {m_file, m_line, m_col}; }

char Lexer::peekChar(size_t offset) const {
    const size_t i = m_pos + offset;
    return i < m_source.size() ? m_source[i] : '\0';
}

char Lexer::takeChar() {
    const char c = peekChar();
    if (c == '\0')
        return c;
    ++m_pos;
    if (c == '\n') {
        ++m_line;
        m_col = 1;
    } else {
        ++m_col;
    }
    return c;
}

void Lexer::skipTrivia() {
    for (;;) {
        const char c = peekChar();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            takeChar();
            continue;
        }
        if (c == '/' && peekChar(1) == '/') {
            while (peekChar() != '\0' && peekChar() != '\n')
                takeChar();
            continue;
        }
        if (c == '/' && peekChar(1) == '*') {
            const SourceLoc start = here();
            takeChar();
            takeChar();
            for (;;) {
                if (peekChar() == '\0')
                    throw Error(start, "unterminated block comment");
                if (peekChar() == '*' && peekChar(1) == '/') {
                    takeChar();
                    takeChar();
                    break;
                }
                takeChar();
            }
            continue;
        }
        break;
    }
}

Token Lexer::makeToken(TokenKind kind, SourceLoc loc, std::string text) {
    Token tok;
    tok.kind = kind;
    tok.text = std::move(text);
    tok.loc = std::move(loc);
    return tok;
}

Token Lexer::lexIdentifier(SourceLoc start) {
    std::string text;
    while (isAlphaNum(peekChar()) || peekChar() == '_')
        text += takeChar();
    return makeToken(TokenKind::Identifier, start, std::move(text));
}

Token Lexer::lexNumber(SourceLoc start) {
    std::string text;
    while (isDigit(peekChar()))
        text += takeChar();
    if (peekChar() == '.' && isDigit(peekChar(1))) {
        text += takeChar(); // '.'
        while (isDigit(peekChar()))
            text += takeChar();
    }
    if (peekChar() == 'e' || peekChar() == 'E') {
        // Exponent, e.g. 1e3 or 1.5e-2 — only consumed if digits follow.
        size_t off = 1;
        char next = peekChar(off);
        if (next == '+' || next == '-') {
            ++off;
            next = peekChar(off);
        }
        if (isDigit(next)) {
            text += takeChar(); // 'e'
            if (peekChar() == '+' || peekChar() == '-')
                text += takeChar();
            while (isDigit(peekChar()))
                text += takeChar();
        }
    }
    Token tok = makeToken(TokenKind::Number, start, std::move(text));
    // Optional unit suffix attached to the number, e.g. 300ms.
    if (isAlpha(peekChar()) || peekChar() == '_') {
        while (isAlphaNum(peekChar()) || peekChar() == '_')
            tok.suffix += takeChar();
    }
    return tok;
}

Token Lexer::lexString(SourceLoc start) {
    takeChar(); // opening quote
    std::string value;
    for (;;) {
        const char c = peekChar();
        if (c == '\0' || c == '\n')
            throw Error(start, "unterminated string literal");
        if (c == '"') {
            takeChar();
            break;
        }
        if (c == '\\') {
            takeChar();
            const SourceLoc escLoc = here();
            const char e = takeChar();
            switch (e) {
            case 'n': value += '\n'; break;
            case 't': value += '\t'; break;
            case 'r': value += '\r'; break;
            case '"': value += '"'; break;
            case '\\': value += '\\'; break;
            case '\0': throw Error(start, "unterminated string literal");
            default:
                throw Error(escLoc, std::string("unknown escape sequence '\\") + e + "'");
            }
            continue;
        }
        value += takeChar();
    }
    return makeToken(TokenKind::String, start, std::move(value));
}

Token Lexer::nextIncludePath() {
    // The path must stay on the directive's line: only horizontal whitespace
    // is skipped here, never newlines.
    while (peekChar() == ' ' || peekChar() == '\t')
        takeChar();
    const SourceLoc start = here();
    const char open = peekChar();
    char close = '\0';
    bool angled = false;
    if (open == '<') {
        close = '>';
        angled = true;
    } else if (open == '"') {
        close = '"';
    } else {
        throw Error(start, "expected '<...>' or '\"...\"' after '#include'");
    }
    takeChar(); // opening delimiter
    std::string path;
    while (peekChar() != '\0' && peekChar() != close && peekChar() != '\n')
        path += takeChar();
    if (peekChar() != close)
        throw Error(start,
                    std::string("unterminated include path (expected '") + close + "')");
    takeChar(); // closing delimiter
    Token tok = makeToken(TokenKind::IncludePath, start, std::move(path));
    tok.angled = angled;
    return tok;
}

Token Lexer::next() {
    skipTrivia();
    const SourceLoc start = here();
    const char c = peekChar();
    if (c == '\0')
        return makeToken(TokenKind::EndOfFile, start);
    if (isAlpha(c) || c == '_')
        return lexIdentifier(start);
    if (isDigit(c))
        return lexNumber(start);
    if (c == '"')
        return lexString(start);

    switch (c) {
    case '#': takeChar(); return makeToken(TokenKind::Hash, start, "#");
    case '{': takeChar(); return makeToken(TokenKind::LBrace, start, "{");
    case '}': takeChar(); return makeToken(TokenKind::RBrace, start, "}");
    case '(': takeChar(); return makeToken(TokenKind::LParen, start, "(");
    case ')': takeChar(); return makeToken(TokenKind::RParen, start, ")");
    case ':': takeChar(); return makeToken(TokenKind::Colon, start, ":");
    case ';': takeChar(); return makeToken(TokenKind::Semicolon, start, ";");
    case ',': takeChar(); return makeToken(TokenKind::Comma, start, ",");
    case '.': takeChar(); return makeToken(TokenKind::Dot, start, ".");
    case '?': takeChar(); return makeToken(TokenKind::Question, start, "?");
    case '+': takeChar(); return makeToken(TokenKind::Plus, start, "+");
    case '-': takeChar(); return makeToken(TokenKind::Minus, start, "-");
    case '*': takeChar(); return makeToken(TokenKind::Star, start, "*");
    case '/': takeChar(); return makeToken(TokenKind::Slash, start, "/");
    case '%': takeChar(); return makeToken(TokenKind::Percent, start, "%");
    case '!':
        takeChar();
        if (peekChar() == '=') {
            takeChar();
            return makeToken(TokenKind::BangEqual, start, "!=");
        }
        return makeToken(TokenKind::Bang, start, "!");
    case '=':
        takeChar();
        if (peekChar() == '=') {
            takeChar();
            return makeToken(TokenKind::EqualEqual, start, "==");
        }
        return makeToken(TokenKind::Equal, start, "=");
    case '<':
        takeChar();
        if (peekChar() == '=') {
            takeChar();
            return makeToken(TokenKind::LessEqual, start, "<=");
        }
        return makeToken(TokenKind::Less, start, "<");
    case '>':
        takeChar();
        if (peekChar() == '=') {
            takeChar();
            return makeToken(TokenKind::GreaterEqual, start, ">=");
        }
        return makeToken(TokenKind::Greater, start, ">");
    case '&':
        takeChar();
        if (peekChar() == '&') {
            takeChar();
            return makeToken(TokenKind::AndAnd, start, "&&");
        }
        throw Error(start, "unexpected '&' (use '&&')");
    case '|':
        takeChar();
        if (peekChar() == '|') {
            takeChar();
            return makeToken(TokenKind::OrOr, start, "||");
        }
        throw Error(start, "unexpected '|' (use '||')");
    default:
        throw Error(start, std::string("unexpected character '") + c + "'");
    }
}

} // namespace Fml
} // namespace Firmir
