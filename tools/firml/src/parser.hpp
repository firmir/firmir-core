#pragma once

#include "ast.hpp"
#include "lexer.hpp"

#include <string>

namespace Firmir {
namespace Fml {

/// Hand-written recursive-descent parser for Firml v0.1 (no external tools).
///
/// Grammar (informal):
///   file        := include* object EOF
///   include     := '#' 'include' ('<' path '>' | '"' path '"')
///   object      := IdentifierUpper '{' member* '}'
///   member      := property | group | anchor | behavior | object
///   property    := Identifier ':' expr
///   group       := IdentifierLower '{' (Identifier ':' expr)* '}'
///                  (sub-property group: `border { width: 2 }` desugars to
///                  `border.width: 2`; 'anchors { ... }' is special-cased)
///   anchor      := 'anchors' ('{' (Identifier ':' expr)* '}' | '.' Identifier ':' expr)
///   behavior    := 'behavior' 'on' Identifier '{' object '}'
///                  (the object is the animation tree: NumberAnimation/... )
///   object      := qualifiedName '{' member* '}'   (qualified: A.B.Name)
///   expr        := conditional ('?' conditional ':' conditional)?
///   conditional := orExpr; standard precedence || && == != < <= > >= + - * / %
///                  unary ('-', '!'), member access (a.b), calls (f(a, b)),
///                  literals (number, string, true, false, undefined)
///
/// Errors are thrown as Fml::Error with an exact file:line:col position.
class Parser {
public:
    /// The source must outlive the parser.
    Parser(const std::string& source, std::string fileName);

    File parseFile();

private:
    const Token& cur() const { return m_cur; }
    TokenKind peekKind(); // one-token lookahead (does not consume)
    void advance();
    bool accept(TokenKind kind);
    Token expect(TokenKind kind, const std::string& what);

    IncludeDirective parseInclude();
    UsingDirective parseUsing();
    ObjectPtr parseObject();
    void parseMember(Object& object);
    void parseAnchorsBlock(Object& object); // anchors { ... } (flattened)
    AnchorStmt parseAnchorEntry();          // key: expr (block entry)
    AnchorStmt parseAnchorShorthand();      // anchors.<key>: expr
    PropertyStmt parseBorderProperty();     // border.<key>: expr
    void parsePropertyGroup(Object& object); // name { key: expr ... }
    BehaviorStmt parseBehavior();           // behavior on <prop> { <anim> }
    PropertyStmt parseProperty();           // name: expr
    PropertyDecl parsePropertyDecl();       // property <type> <name>[: expr]
    HandlerStmt parseHandler();             // onSignal(...) / onPropertyChanged
    FunctionDecl parseFunction();           // function <name>(<arg>: <type>, ...) [-> <type>] { ... }
    Expr parseCommand();                    // one handler/function body command

    Expr parseExpression(); // alias for the top of the expression grammar
    Expr parseConditional();
    Expr parseOr();
    Expr parseAnd();
    Expr parseEquality();
    Expr parseRelational();
    Expr parseAdditive();
    Expr parseMultiplicative();
    Expr parseUnary();
    Expr parsePostfix();
    Expr parsePrimary();

    std::string m_file;
    Lexer m_lexer;
    Token m_cur;
    Token m_peek;
    bool m_peeked = false;
};

} // namespace Fml
} // namespace Firmir
