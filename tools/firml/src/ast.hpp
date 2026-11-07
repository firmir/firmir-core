#pragma once

#include "error.hpp"

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace Firmir {
namespace Fml {

// ------------------------------- expressions -------------------------------

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

/// Number literal; `text` is the raw digits ("12.5"), `suffix` the optional
/// unit ("ms" for durations). Value interpretation is a semantic (M2) concern.
struct NumberLit {
    std::string text;
    std::string suffix;
};
struct StringLit {
    std::string value; // decoded (escapes resolved)
};
struct BoolLit {
    bool value;
};
struct UndefinedLit {}; // `undefined` (anchor reset)
struct IdentExpr {
    std::string name; // plain identifier: root, width, parent, OutCubic, ...
};
struct MemberExpr {
    ExprPtr object; // root.width -> object=(ident root), name="width"
    std::string name;
};
struct CallExpr {
    std::string callee; // builtin functions only (min, max, lighter, darker)
    std::vector<Expr> args;
};
/// object.method(args) — used by signal/property handler commands.
struct MethodCallExpr {
    ExprPtr object;
    std::string method;
    std::vector<Expr> args;
};
/// `target = value` — a property assignment command inside a handler body;
/// target is an IdentExpr (own property) or MemberExpr (id.prop / parent.prop).
struct AssignExpr {
    ExprPtr target;
    ExprPtr value;
};
struct UnaryExpr {
    char op; // '-' or '!'
    ExprPtr operand;
};
struct BinaryExpr {
    std::string op; // || && == != < <= > >= + - * / %
    ExprPtr lhs;
    ExprPtr rhs;
};
struct ConditionalExpr {
    ExprPtr condition;
    ExprPtr thenExpr;
    ExprPtr elseExpr;
};

using ExprNode = std::variant<NumberLit, StringLit, BoolLit, UndefinedLit,
                              IdentExpr, MemberExpr, CallExpr, MethodCallExpr,
                              AssignExpr, UnaryExpr, BinaryExpr, ConditionalExpr>;

struct Expr {
    SourceLoc loc;
    ExprNode node;

    Expr(SourceLoc loc, ExprNode node) : loc(std::move(loc)), node(std::move(node)) {}
};

inline ExprPtr makeExpr(Expr expr) { return std::make_unique<Expr>(std::move(expr)); }

// --------------------------------- members ---------------------------------

struct Object;
using ObjectPtr = std::unique_ptr<Object>;

/// `name: <expr>` — also used for `id: <name>` and for animation settings.
struct PropertyStmt {
    SourceLoc loc;
    std::string name;
    Expr value;
};

/// One anchor assignment. Both forms parse into this node:
///   anchors { left: parent.left }   (block entries are flattened)
///   anchors.fill: parent            (shorthand)
struct AnchorStmt {
    SourceLoc loc;
    std::string key; // left, right, ..., fill, centerIn, leftMargin, ...
    Expr value;
};

/// `behavior on <property> { <animation tree> }` — animates every subsequent
/// change of the property with the given animation (QML Behavior analogue).
/// The animation tree reuses the Object node: NumberAnimation/ColorAnimation/
/// PauseAnimation leaves with `duration`/`easing` settings, SequentialAnimation/
/// ParallelAnimation containers with child animations.
struct BehaviorStmt {
    SourceLoc loc;
    std::string property;
    ObjectPtr animation;
};

/// Handler argument: `int code`, `string message`, ...
struct HandlerParam {
    SourceLoc loc;
    std::string typeName;
    std::string name;
};

/// `onSignal(...) { object.method(...) }` or `onPropertyChanged: ...`.
struct HandlerStmt {
    SourceLoc loc;
    std::string name; // exactly as written, including the leading "on"
    std::vector<HandlerParam> params;
    std::vector<Expr> commands; // each expression is validated as MethodCallExpr
};

using MemberNode = std::variant<PropertyStmt, AnchorStmt, BehaviorStmt, HandlerStmt,
                                ObjectPtr>;

/// `TypeName { <members> }` — a Firml object instance.
struct Object {
    SourceLoc loc;
    std::string typeName;
    std::vector<MemberNode> members;
};

// ---------------------------------- file -----------------------------------

struct IncludeDirective {
    SourceLoc loc;
    std::string path;
    bool angled = false; // <path> vs "path"
};

/// `using Alias = Firmir.Gui.Item` — a file-local type alias.
struct UsingDirective {
    SourceLoc loc;
    std::string alias;
    std::string typeName;
};

/// A parsed .fml file: #include directives followed by one root object.
struct File {
    std::string fileName;
    std::vector<IncludeDirective> includes;
    std::vector<UsingDirective> usings;
    ObjectPtr root; // exactly one root object (enforced by the parser)
};

} // namespace Fml
} // namespace Firmir
