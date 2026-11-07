#include "parser.hpp"

namespace Firmir {
namespace Fml {

namespace {

std::string describe(const Token& tok) {
    switch (tok.kind) {
    case TokenKind::EndOfFile: return "end of file";
    case TokenKind::Identifier: return "'" + tok.text + "'";
    case TokenKind::Number: return "'" + tok.text + tok.suffix + "'";
    case TokenKind::String: return "a string literal";
    case TokenKind::IncludePath: return "an include path";
    default: return "'" + tok.text + "'"; // punctuators carry their text
    }
}

bool isUppercase(const std::string& name) {
    return !name.empty() && name[0] >= 'A' && name[0] <= 'Z';
}

} // namespace

Parser::Parser(const std::string& source, std::string fileName)
    : m_file(fileName), m_lexer(source, std::move(fileName)) {
    advance(); // prime m_cur with the first token
}

TokenKind Parser::peekKind() {
    if (!m_peeked) {
        m_peek = m_lexer.next();
        m_peeked = true;
    }
    return m_peek.kind;
}

void Parser::advance() {
    if (m_peeked) {
        m_cur = std::move(m_peek);
        m_peeked = false;
    } else {
        m_cur = m_lexer.next();
    }
}

bool Parser::accept(TokenKind kind) {
    if (m_cur.kind != kind)
        return false;
    advance();
    return true;
}

Token Parser::expect(TokenKind kind, const std::string& what) {
    if (m_cur.kind != kind)
        throw Error(m_cur.loc, "expected " + what + ", got " + describe(m_cur));
    Token tok = m_cur;
    advance();
    return tok;
}

// ------------------------------- file level -------------------------------

File Parser::parseFile() {
    File file;
    file.fileName = m_file;

    while (m_cur.kind == TokenKind::Hash)
        file.includes.push_back(parseInclude());
    while (m_cur.kind == TokenKind::Identifier && m_cur.text == "using")
        file.usings.push_back(parseUsing());

    if (m_cur.kind == TokenKind::EndOfFile)
        throw Error(m_cur.loc, "empty file: expected a root object (TypeName { ... })");
    file.root = parseObject();

    while (accept(TokenKind::Semicolon)) {
    }
    if (m_cur.kind == TokenKind::Hash)
        throw Error(m_cur.loc, "'#include' directives must precede the root object");
    if (m_cur.kind != TokenKind::EndOfFile)
        throw Error(m_cur.loc, "unexpected " + describe(m_cur) +
                                   " after the root object (exactly one root object is allowed)");
    return file;
}

IncludeDirective Parser::parseInclude() {
    const SourceLoc hashLoc = m_cur.loc;
    advance(); // '#'
    if (m_cur.kind != TokenKind::Identifier || m_cur.text != "include")
        throw Error(m_cur.loc, "expected 'include' after '#'");
    // The include path is lexed in a special mode (no tokenization inside).
    Token path = m_lexer.nextIncludePath();
    advance(); // move past the path token

    IncludeDirective inc;
    inc.loc = hashLoc;
    inc.path = std::move(path.text);
    inc.angled = path.angled;
    return inc;
}

UsingDirective Parser::parseUsing() {
    const SourceLoc loc = m_cur.loc;
    advance(); // using
    const Token alias = expect(TokenKind::Identifier, "an alias after 'using'");
    if (!isUppercase(alias.text))
        throw Error(alias.loc, "a type alias must start with an uppercase letter");
    expect(TokenKind::Equal, "'=' after the type alias");
    std::string typeName =
        expect(TokenKind::Identifier, "a type name after '='").text;
    while (m_cur.kind == TokenKind::Dot) {
        advance();
        typeName += "." + expect(TokenKind::Identifier,
                                  "a type name segment after '.'").text;
    }
    accept(TokenKind::Semicolon);
    return UsingDirective{loc, alias.text, std::move(typeName)};
}

// -------------------------------- objects ---------------------------------

ObjectPtr Parser::parseObject() {
    const Token typeTok = expect(TokenKind::Identifier, "a type name");
    // Qualified type names use dots: Firmir.Gui.Rectangle -> ::-path in C++.
    std::string typeName = typeTok.text;
    while (m_cur.kind == TokenKind::Dot) {
        advance();
        const Token part =
            expect(TokenKind::Identifier, "a type name segment after '.'");
        typeName += "." + part.text;
    }
    const std::string lastSegment = typeName.substr(typeName.rfind('.') + 1);
    if (!isUppercase(lastSegment))
        throw Error(typeTok.loc,
                    "type name must start with an uppercase letter, got '" + typeName + "'");
    expect(TokenKind::LBrace, "'{' after the type name");

    auto object = std::make_unique<Object>();
    object->loc = typeTok.loc;
    object->typeName = std::move(typeName);

    for (;;) {
        while (accept(TokenKind::Semicolon)) {
        }
        if (m_cur.kind == TokenKind::RBrace) {
            advance();
            break;
        }
        if (m_cur.kind == TokenKind::EndOfFile)
            throw Error(m_cur.loc, "unexpected end of file, expected a member or '}' to close '" +
                                       object->typeName + "'");
        parseMember(*object);
    }
    return object;
}

void Parser::parseMember(Object& object) {
    if (m_cur.kind != TokenKind::Identifier)
        throw Error(m_cur.loc, "expected a property, an object, 'anchors' or 'animate', got " +
                                   describe(m_cur));

    if (m_cur.text == "anchors" && peekKind() == TokenKind::LBrace) {
        parseAnchorsBlock(object);
        return;
    }
    if (m_cur.text == "anchors" && peekKind() == TokenKind::Dot) {
        object.members.push_back(parseAnchorShorthand());
        return;
    }
    if (m_cur.text == "border" && peekKind() == TokenKind::Dot) {
        // Grouped border properties: border.color / border.width.
        object.members.push_back(parseBorderProperty());
        return;
    }
    if (m_cur.text == "behavior" && peekKind() == TokenKind::Identifier &&
        m_peek.text == "on") {
        object.members.push_back(parseBehavior());
        return;
    }
    if (m_cur.text == "property" && peekKind() == TokenKind::Identifier) {
        object.members.push_back(parsePropertyDecl());
        return;
    }
    if (m_cur.text.size() > 2 && m_cur.text.compare(0, 2, "on") == 0 &&
        (isUppercase(m_cur.text.substr(2, 1)) ||
         (m_cur.text.size() > 8 &&
          m_cur.text.compare(m_cur.text.size() - 7, 7, "Changed") == 0)) &&
        (peekKind() == TokenKind::Colon || peekKind() == TokenKind::LParen)) {
        object.members.push_back(parseHandler());
        return;
    }

    if (peekKind() == TokenKind::LBrace || peekKind() == TokenKind::Dot) {
        // `Type { ... }` or a qualified type name `A.B.Type { ... }` ('anchors'
        // with a dot was handled above as the shorthand form).
        object.members.push_back(parseObject());
        return;
    }
    object.members.push_back(parseProperty());
}

void Parser::parseAnchorsBlock(Object& object) {
    advance(); // 'anchors'
    advance(); // '{'
    for (;;) {
        while (accept(TokenKind::Semicolon)) {
        }
        if (m_cur.kind == TokenKind::RBrace) {
            advance();
            break;
        }
        if (m_cur.kind == TokenKind::EndOfFile)
            throw Error(m_cur.loc, "unexpected end of file, expected an anchor or '}' "
                                   "to close the 'anchors' block");
        object.members.push_back(parseAnchorEntry());
    }
}

AnchorStmt Parser::parseAnchorEntry() {
    const Token keyTok =
        expect(TokenKind::Identifier, "an anchor name (left, right, ..., fill, centerIn)");
    expect(TokenKind::Colon, "':' after the anchor name");
    Expr value = parseExpression();
    accept(TokenKind::Semicolon);
    return AnchorStmt{keyTok.loc, keyTok.text, std::move(value)};
}

AnchorStmt Parser::parseAnchorShorthand() {
    advance(); // 'anchors'
    advance(); // '.'
    const Token keyTok = expect(TokenKind::Identifier, "an anchor name after 'anchors.'");
    expect(TokenKind::Colon, "':' after the anchor name");
    Expr value = parseExpression();
    accept(TokenKind::Semicolon);
    return AnchorStmt{keyTok.loc, keyTok.text, std::move(value)};
}

PropertyStmt Parser::parseBorderProperty() {
    advance(); // 'border'
    advance(); // '.'
    const Token keyTok =
        expect(TokenKind::Identifier, "'color' or 'width' after 'border.'");
    expect(TokenKind::Colon, "':' after 'border.<name>'");
    Expr value = parseExpression();
    accept(TokenKind::Semicolon);
    return PropertyStmt{keyTok.loc, "border." + keyTok.text, std::move(value)};
}

BehaviorStmt Parser::parseBehavior() {
    const SourceLoc keywordLoc = m_cur.loc;
    advance(); // 'behavior'
    advance(); // 'on'
    const Token propTok =
        expect(TokenKind::Identifier, "a property name after 'behavior on'");
    expect(TokenKind::LBrace, "'{' after the behavior target");
    if (m_cur.kind == TokenKind::RBrace)
        throw Error(m_cur.loc, "empty behavior block: expected an animation "
                               "(e.g. NumberAnimation { duration: 250ms })");
    ObjectPtr animation = parseObject();
    expect(TokenKind::RBrace, "'}' to close the behavior block");
    accept(TokenKind::Semicolon);
    return BehaviorStmt{keywordLoc, propTok.text, std::move(animation)};
}

HandlerStmt Parser::parseHandler() {
    HandlerStmt handler;
    const Token name = m_cur;
    handler.loc = name.loc;
    handler.name = name.text;
    advance();
    if (accept(TokenKind::LParen)) {
        if (m_cur.kind != TokenKind::RParen) {
            for (;;) {
                const Token type =
                    expect(TokenKind::Identifier, "a handler argument type");
                const Token arg =
                    expect(TokenKind::Identifier, "a handler argument name");
                handler.params.push_back({type.loc, type.text, arg.text});
                if (!accept(TokenKind::Comma))
                    break;
            }
        }
        expect(TokenKind::RParen, "')' after handler arguments");
    }
    expect(TokenKind::Colon, "':' after the handler name");
    auto parseCommand = [this, &handler] {
        Expr command = parseExpression();
        if (m_cur.kind == TokenKind::Equal) {
            // Property assignment command: `<target> = <expr>`; the target
            // shape (id.prop / parent.prop / own property) is checked by sema.
            const SourceLoc loc = command.loc;
            advance();
            Expr value = parseExpression();
            command = Expr(loc, AssignExpr{makeExpr(std::move(command)),
                                           makeExpr(std::move(value))});
        }
        accept(TokenKind::Semicolon);
        handler.commands.push_back(std::move(command));
    };
    if (accept(TokenKind::LBrace)) {
        while (m_cur.kind != TokenKind::RBrace) {
            if (m_cur.kind == TokenKind::EndOfFile)
                throw Error(m_cur.loc, "unterminated handler block");
            parseCommand();
        }
        advance();
    } else {
        parseCommand();
    }
    return handler;
}

PropertyStmt Parser::parseProperty() {
    const Token nameTok = expect(TokenKind::Identifier, "a property name");
    expect(TokenKind::Colon, "':' after property '" + nameTok.text + "'");
    Expr value = parseExpression();
    accept(TokenKind::Semicolon);
    return PropertyStmt{nameTok.loc, nameTok.text, std::move(value)};
}

PropertyDecl Parser::parsePropertyDecl() {
    // `property <type> <name>[: <expr>]` — a script-declared dynamic property
    // (declarative style). The type validity is a semantic (M2) concern.
    const SourceLoc loc = m_cur.loc;
    advance(); // 'property'
    const Token typeTok =
        expect(TokenKind::Identifier,
               "a property type ('number', 'string', 'bool' or 'color')");
    const Token nameTok = expect(TokenKind::Identifier, "a property name");
    PropertyDecl decl;
    decl.loc = loc;
    decl.typeName = typeTok.text;
    decl.name = nameTok.text;
    decl.init.loc = loc;
    decl.init.name = decl.name;
    if (accept(TokenKind::Colon)) {
        decl.init.value = parseExpression();
        decl.hasValue = true;
    }
    accept(TokenKind::Semicolon);
    return decl;
}

// ------------------------------- expressions ------------------------------

Expr Parser::parseExpression() { return parseConditional(); }

Expr Parser::parseConditional() {
    Expr cond = parseOr();
    if (m_cur.kind != TokenKind::Question)
        return cond;
    const SourceLoc loc = cond.loc;
    advance(); // '?'
    Expr thenExpr = parseConditional();
    expect(TokenKind::Colon, "':' in the conditional (?:) expression");
    Expr elseExpr = parseConditional();
    return Expr(loc, ConditionalExpr{makeExpr(std::move(cond)), makeExpr(std::move(thenExpr)),
                                     makeExpr(std::move(elseExpr))});
}

Expr Parser::parseOr() {
    Expr lhs = parseAnd();
    while (m_cur.kind == TokenKind::OrOr) {
        const SourceLoc loc = lhs.loc; // copy before std::move(lhs) below
        advance();
        Expr rhs = parseAnd();
        lhs = Expr(loc, BinaryExpr{"||", makeExpr(std::move(lhs)), makeExpr(std::move(rhs))});
    }
    return lhs;
}

Expr Parser::parseAnd() {
    Expr lhs = parseEquality();
    while (m_cur.kind == TokenKind::AndAnd) {
        const SourceLoc loc = lhs.loc;
        advance();
        Expr rhs = parseEquality();
        lhs = Expr(loc, BinaryExpr{"&&", makeExpr(std::move(lhs)), makeExpr(std::move(rhs))});
    }
    return lhs;
}

Expr Parser::parseEquality() {
    Expr lhs = parseRelational();
    while (m_cur.kind == TokenKind::EqualEqual || m_cur.kind == TokenKind::BangEqual) {
        const SourceLoc loc = lhs.loc;
        const std::string op = m_cur.text;
        advance();
        Expr rhs = parseRelational();
        lhs = Expr(loc, BinaryExpr{op, makeExpr(std::move(lhs)), makeExpr(std::move(rhs))});
    }
    return lhs;
}

Expr Parser::parseRelational() {
    Expr lhs = parseAdditive();
    while (m_cur.kind == TokenKind::Less || m_cur.kind == TokenKind::LessEqual ||
           m_cur.kind == TokenKind::Greater || m_cur.kind == TokenKind::GreaterEqual) {
        const SourceLoc loc = lhs.loc;
        const std::string op = m_cur.text;
        advance();
        Expr rhs = parseAdditive();
        lhs = Expr(loc, BinaryExpr{op, makeExpr(std::move(lhs)), makeExpr(std::move(rhs))});
    }
    return lhs;
}

Expr Parser::parseAdditive() {
    Expr lhs = parseMultiplicative();
    while (m_cur.kind == TokenKind::Plus || m_cur.kind == TokenKind::Minus) {
        const SourceLoc loc = lhs.loc;
        const std::string op = m_cur.text;
        advance();
        Expr rhs = parseMultiplicative();
        lhs = Expr(loc, BinaryExpr{op, makeExpr(std::move(lhs)), makeExpr(std::move(rhs))});
    }
    return lhs;
}

Expr Parser::parseMultiplicative() {
    Expr lhs = parseUnary();
    while (m_cur.kind == TokenKind::Star || m_cur.kind == TokenKind::Slash ||
           m_cur.kind == TokenKind::Percent) {
        const SourceLoc loc = lhs.loc;
        const std::string op = m_cur.text;
        advance();
        Expr rhs = parseUnary();
        lhs = Expr(loc, BinaryExpr{op, makeExpr(std::move(lhs)), makeExpr(std::move(rhs))});
    }
    return lhs;
}

Expr Parser::parseUnary() {
    if (m_cur.kind == TokenKind::Bang || m_cur.kind == TokenKind::Minus) {
        const char op = (m_cur.kind == TokenKind::Bang) ? '!' : '-';
        const SourceLoc loc = m_cur.loc;
        advance();
        return Expr(loc, UnaryExpr{op, makeExpr(parseUnary())});
    }
    return parsePostfix();
}

Expr Parser::parsePostfix() {
    Expr expr = parsePrimary();
    while (m_cur.kind == TokenKind::Dot) {
        const SourceLoc loc = expr.loc; // copy before std::move(expr) below
        advance();
        const Token nameTok = expect(TokenKind::Identifier, "a member name after '.'");
        if (m_cur.kind == TokenKind::LParen) {
            advance();
            MethodCallExpr call;
            call.object = makeExpr(std::move(expr));
            call.method = nameTok.text;
            if (m_cur.kind != TokenKind::RParen) {
                for (;;) {
                    call.args.push_back(parseConditional());
                    if (!accept(TokenKind::Comma))
                        break;
                }
            }
            expect(TokenKind::RParen, "')' after method arguments");
            expr = Expr(loc, std::move(call));
        } else {
            expr = Expr(loc, MemberExpr{makeExpr(std::move(expr)), nameTok.text});
        }
    }
    return expr;
}

Expr Parser::parsePrimary() {
    const Token tok = m_cur;
    switch (m_cur.kind) {
    case TokenKind::Number:
        advance();
        if (!tok.suffix.empty() && tok.suffix != "ms")
            throw Error(tok.loc,
                        "unknown numeric suffix '" + tok.suffix + "' (supported: 'ms')");
        return Expr(tok.loc, NumberLit{tok.text, tok.suffix});
    case TokenKind::String:
        advance();
        return Expr(tok.loc, StringLit{tok.text});
    case TokenKind::Identifier:
        if (tok.text == "true" || tok.text == "false") {
            advance();
            return Expr(tok.loc, BoolLit{tok.text == "true"});
        }
        if (tok.text == "undefined") {
            advance();
            return Expr(tok.loc, UndefinedLit{});
        }
        if (peekKind() == TokenKind::LParen) {
            // Function-style call, e.g. min(a, b). Semantic analysis (M2)
            // restricts the callee to the builtin functions.
            advance(); // callee
            advance(); // '('
            CallExpr call;
            call.callee = tok.text;
            if (m_cur.kind != TokenKind::RParen) {
                for (;;) {
                    call.args.push_back(parseConditional());
                    if (!accept(TokenKind::Comma))
                        break;
                }
            }
            expect(TokenKind::RParen, "')' after the arguments of '" + tok.text + "'");
            return Expr(tok.loc, std::move(call));
        }
        advance();
        return Expr(tok.loc, IdentExpr{tok.text});
    case TokenKind::LParen: {
        advance();
        Expr inner = parseConditional();
        expect(TokenKind::RParen, "')' to close the parenthesized expression");
        return inner;
    }
    default:
        throw Error(m_cur.loc, "expected an expression, got " + describe(m_cur));
    }
}

} // namespace Fml
} // namespace Firmir
