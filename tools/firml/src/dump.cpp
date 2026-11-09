#include "dump.hpp"

#include "sema.hpp"

#include <ostream>
#include <string>

namespace Firmir {
namespace Fml {

namespace {

std::string escapeString(const std::string& value) {
    std::string out;
    for (const char c : value) {
        switch (c) {
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string pos(const SourceLoc& loc) {
    return std::to_string(loc.line) + ":" + std::to_string(loc.col);
}

void dumpExpr(const Expr& expr, std::ostream& out);
void dumpMember(const MemberNode& member, std::ostream& out, int indent);
void dumpObject(const Object& object, std::ostream& out, int indent);

void dumpExpr(const Expr& expr, std::ostream& out) {
    struct Visitor {
        std::ostream& out;

        void operator()(const NumberLit& number) const {
            out << "(number " << number.text << number.suffix << ")";
        }
        void operator()(const StringLit& string) const {
            out << "(string \"" << escapeString(string.value) << "\")";
        }
        void operator()(const BoolLit& boolean) const {
            out << "(bool " << (boolean.value ? "true" : "false") << ")";
        }
        void operator()(const UndefinedLit&) const { out << "(undefined)"; }
        void operator()(const IdentExpr& ident) const { out << "(ident " << ident.name << ")"; }
        void operator()(const MemberExpr& member) const {
            out << "(member ";
            dumpExpr(*member.object, out);
            out << " " << member.name << ")";
        }
        void operator()(const CallExpr& call) const {
            out << "(call " << call.callee;
            for (const Expr& arg : call.args) {
                out << " ";
                dumpExpr(arg, out);
            }
            out << ")";
        }
        void operator()(const MethodCallExpr& call) const {
            out << "(method ";
            dumpExpr(*call.object, out);
            out << " " << call.method;
            for (const Expr& arg : call.args) {
                out << " ";
                dumpExpr(arg, out);
            }
            out << ")";
        }
        void operator()(const AssignExpr& assign) const {
            out << "(assign ";
            dumpExpr(*assign.target, out);
            out << " ";
            dumpExpr(*assign.value, out);
            out << ")";
        }
        void operator()(const ConstDeclExpr& decl) const {
            out << "(const " << decl.name << " ";
            dumpExpr(*decl.value, out);
            out << ")";
        }
        void operator()(const UnaryExpr& unary) const {
            out << "(unary " << unary.op << " ";
            dumpExpr(*unary.operand, out);
            out << ")";
        }
        void operator()(const BinaryExpr& binary) const {
            out << "(binary " << binary.op << " ";
            dumpExpr(*binary.lhs, out);
            out << " ";
            dumpExpr(*binary.rhs, out);
            out << ")";
        }
        void operator()(const ConditionalExpr& cond) const {
            out << "(cond ";
            dumpExpr(*cond.condition, out);
            out << " ";
            dumpExpr(*cond.thenExpr, out);
            out << " ";
            dumpExpr(*cond.elseExpr, out);
            out << ")";
        }
    };
    std::visit(Visitor{out}, expr.node);
}

void dumpMember(const MemberNode& member, std::ostream& out, int indent) {
    const std::string pad(static_cast<size_t>(indent), ' ');
    struct Visitor {
        std::ostream& out;
        const std::string& pad;
        int indent;

        void operator()(const PropertyStmt& prop) const {
            out << pad << "(property " << prop.name << " " << pos(prop.loc) << " ";
            dumpExpr(prop.value, out);
            out << ")\n";
        }
        void operator()(const PropertyDecl& decl) const {
            out << pad << "(decl " << decl.typeName << " " << decl.name << " "
                << pos(decl.loc);
            if (decl.hasValue) {
                out << " ";
                dumpExpr(decl.init.value, out);
            }
            out << ")\n";
        }
        void operator()(const AnchorStmt& anchor) const {
            out << pad << "(anchor " << anchor.key << " " << pos(anchor.loc) << " ";
            dumpExpr(anchor.value, out);
            out << ")\n";
        }
        void operator()(const BehaviorStmt& behavior) const {
            out << pad << "(behavior " << behavior.property << " " << pos(behavior.loc)
                << "\n";
            dumpObject(*behavior.animation, out, indent + 2);
            out << pad << ")\n";
        }
        void operator()(const HandlerStmt& handler) const {
            out << pad << "(handler " << handler.name << " " << pos(handler.loc);
            for (const HandlerParam& param : handler.params)
                out << " (param " << param.typeName << " " << param.name << ")";
            out << "\n";
            for (const Expr& command : handler.commands)
                dumpExpr(command, out);
            out << pad << ")\n";
        }
        void operator()(const FunctionDecl& fn) const {
            out << pad << "(function " << fn.name << " " << pos(fn.loc);
            for (const HandlerParam& param : fn.params)
                out << " (param " << param.typeName << " " << param.name << ")";
            if (!fn.returnTypeName.empty())
                out << " -> " << fn.returnTypeName;
            out << "\n";
            for (const Expr& command : fn.commands)
                dumpExpr(command, out);
            if (fn.returnValue) {
                out << "(return ";
                dumpExpr(*fn.returnValue, out);
                out << ")";
            }
            out << pad << ")\n";
        }
        void operator()(const ObjectPtr& child) const { dumpObject(*child, out, indent); }
    };
    std::visit(Visitor{out, pad, indent}, member);
}

void dumpObject(const Object& object, std::ostream& out, int indent) {
    const std::string pad(static_cast<size_t>(indent), ' ');
    out << pad << "(object " << object.typeName << " " << pos(object.loc);
    if (object.members.empty()) {
        out << ")\n";
        return;
    }
    out << "\n";
    for (const MemberNode& member : object.members)
        dumpMember(member, out, indent + 2);
    out << pad << ")\n";
}

} // namespace

void dumpAst(const File& file, std::ostream& out) {
    out << "(file " << file.fileName << "\n";
    for (const IncludeDirective& inc : file.includes) {
        out << "  (include " << (inc.angled ? "<" : "\"") << inc.path
            << (inc.angled ? ">" : "\"") << " " << pos(inc.loc) << ")\n";
    }
    for (const UsingDirective& use : file.usings)
        out << "  (using " << use.alias << " " << use.typeName << " "
            << pos(use.loc) << ")\n";
    if (file.root)
        dumpObject(*file.root, out, 2);
    out << ")\n";
}

/// One-line summary of a behavior animation tree:
/// (NumberAnimation duration=200ms easing=OutCubic),
/// (SequentialAnimation (PauseAnimation duration=50ms) (...)).
void dumpAnimationSummary(const Object& animation, std::ostream& out) {
    out << "(" << animation.typeName;
    for (const MemberNode& member : animation.members) {
        if (const auto* setting = std::get_if<PropertyStmt>(&member)) {
            out << " " << setting->name << "=";
            if (const auto* number = std::get_if<NumberLit>(&setting->value.node))
                out << number->text << number->suffix;
            else if (const auto* ident = std::get_if<IdentExpr>(&setting->value.node))
                out << ident->name;
        } else if (const auto* child = std::get_if<ObjectPtr>(&member)) {
            out << " ";
            dumpAnimationSummary(**child, out);
        }
    }
    out << ")";
}

void dumpSema(const File& file, const SemaResult& result, std::ostream& out) {
    out << "(sema " << file.fileName << "\n";
    for (const ObjectInfo* obj : result.objects)
        out << "  (symbol " << obj->internalName << " " << obj->type->name << " "
            << pos(obj->node->loc) << ")\n";
    for (const BindingInfo& binding : result.bindings) {
        out << "  (" << (binding.isStatic ? "set" : "bind") << " "
            << binding.object->internalName << "." << binding.property;
        if (!binding.deps.empty()) {
            out << " ->";
            for (const std::string& dep : binding.deps)
                out << " " << dep;
        }
        out << ")\n";
    }
    for (const AnchorInfo& anchor : result.anchors) {
        out << "  (anchor " << anchor.object->internalName << "." << anchor.key;
        if (!anchor.targetText.empty())
            out << " " << anchor.targetText;
        if (!anchor.deps.empty()) {
            out << " ->";
            for (const std::string& dep : anchor.deps)
                out << " " << dep;
        }
        out << ")\n";
    }
    for (const HandlerInfo& handler : result.handlers) {
        out << "  (" << (handler.kind == HandlerKind::Signal ? "signal-handler"
                                                              : "property-handler")
            << " " << handler.object->internalName << "." << handler.name;
        for (const HandlerParam& param : handler.stmt->params)
            out << " (" << param.typeName << " " << param.name << ")";
        out << ")\n";
    }
    for (const ObjectInfo* obj : result.objects) {
        for (const FunctionInfo& fn : obj->functions) {
            out << "  (function " << obj->internalName << "." << fn.decl->name;
            for (const HandlerParam& param : fn.decl->params)
                out << " (" << param.typeName << " " << param.name << ")";
            out << " -> " << (fn.isVoid ? "void" : valueTypeName(fn.returnType));
            if (!fn.returnDeps.empty()) {
                out << " deps:";
                for (const std::string& dep : fn.returnDeps)
                    out << " " << dep;
            }
            out << ")\n";
        }
    }
    for (const BehaviorInfo& behavior : result.behaviors) {
        out << "  (behavior " << behavior.object->internalName << "." << behavior.property
            << " ";
        dumpAnimationSummary(*behavior.animation, out);
        out << ")\n";
    }
    out << ")\n";
}

} // namespace Fml
} // namespace Firmir
