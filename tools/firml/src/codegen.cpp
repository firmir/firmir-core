#include "codegen.hpp"

#include "types.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <sstream>
#include <utility>

namespace Firmir {
namespace Fml {
namespace {

/// "20_sema_basic" -> "F20SemaBasic", "app" -> "App".
std::string capitalizeIdentifier(const std::string& stem) {
    std::string out;
    bool upperNext = true;
    for (const char c : stem) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out += upperNext
                       ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                       : c;
            upperNext = false;
        } else {
            upperNext = true;
        }
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])))
        out = "F" + out;
    return out;
}

/// The C++ type for an object: dotted names map '.' -> '::', short builtin
/// names live in Firmir::Gui, short custom names stay as written.
std::string cppTypeName(const ObjectInfo& obj) {
    const std::string& typeName = obj.resolvedTypeName.empty()
                                      ? obj.node->typeName
                                      : obj.resolvedTypeName;
    if (typeName.find('.') != std::string::npos) {
        std::string out;
        out.reserve(typeName.size() * 2);
        for (const char c : typeName) {
            if (c == '.')
                out += "::";
            else
                out += c;
        }
        return out;
    }
    if (builtinType(typeName))
        return "Firmir::Gui::" + typeName;
    return typeName;
}

std::string anchorSetterName(const std::string& key) {
    if (key == "left") return "setAnchorLeft";
    if (key == "right") return "setAnchorRight";
    if (key == "horizontalCenter") return "setAnchorHorizontalCenter";
    if (key == "top") return "setAnchorTop";
    if (key == "bottom") return "setAnchorBottom";
    return "setAnchorVerticalCenter"; // verticalCenter
}

std::string anchorLineName(const std::string& line) {
    if (line == "left") return "Left";
    if (line == "right") return "Right";
    if (line == "horizontalCenter") return "HorizontalCenter";
    if (line == "top") return "Top";
    if (line == "bottom") return "Bottom";
    return "VerticalCenter"; // verticalCenter
}

std::string marginSetterName(const std::string& key) {
    if (key == "leftMargin") return "setLeftMargin";
    if (key == "rightMargin") return "setRightMargin";
    if (key == "topMargin") return "setTopMargin";
    if (key == "bottomMargin") return "setBottomMargin";
    if (key == "margins") return "setMargins";
    if (key == "horizontalCenterOffset") return "setHorizontalCenterOffset";
    return "setVerticalCenterOffset"; // verticalCenterOffset
}

bool isColorLiteralText(const std::string& value) {
    if (value.size() != 7 || value[0] != '#')
        return false;
    for (size_t i = 1; i < value.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(value[i])))
            return false;
    return true;
}

/// C++ escaping for a decoded string literal (without the quotes).
std::string escapeCpp(const std::string& text) {
    std::string out;
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[5];
                std::snprintf(buf, sizeof(buf), "\\x%02x", c);
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

/// Number literal text -> double literal ("400" -> "400.0", "12.5" -> "12.5").
std::string numberLiteral(const NumberLit& lit, const SourceLoc& loc) {
    if (!lit.suffix.empty())
        throw Error(loc, "unit suffix '" + lit.suffix +
                             "' is only valid for animate durations");
    if (lit.text.find('.') == std::string::npos &&
        lit.text.find('e') == std::string::npos &&
        lit.text.find('E') == std::string::npos)
        return lit.text + ".0";
    return lit.text;
}

std::string colorLiteralExpr(const std::string& text) {
    // text is validated by isColorLiteralText at the call site.
    return "Firmir::Gui::Color::rgb(0x" + text.substr(1, 2) + ", 0x" +
           text.substr(3, 2) + ", 0x" + text.substr(5, 2) + ")";
}

/// The C++ type used to read a value of the given Firml type out of the
/// property system.
std::string cppValueType(ValueType type) {
    switch (type) {
    case ValueType::Number: return "double";
    case ValueType::Bool: return "bool";
    case ValueType::String: return "Firmir::String";
    case ValueType::Color: return "Firmir::Gui::Color";
    case ValueType::Rect: return "Firmir::Gui::RectF";
    }
    return "double";
}

/// Emits an expression as C++. `type` is the context type of the expression
/// (property type; Number/Bool for sub-expressions); `scope` is the object
/// the expression belongs to (needed for id/parent/own-property references).
/// Member access reads through the property system: anyConvert<T>(obj->getProperty("p")).
class ExprEmitter {
public:
    bool usesAlgorithm = false;
    bool usesCmath = false;
    bool usesPainter = false;
    std::map<std::string, ValueType> locals;

    std::string emit(const Expr& expr, ValueType type, const ObjectInfo& scope) {
        if (const auto* lit = std::get_if<NumberLit>(&expr.node))
            return numberLiteral(*lit, expr.loc);
        if (const auto* lit = std::get_if<StringLit>(&expr.node)) {
            if (type == ValueType::Color) {
                if (!isColorLiteralText(lit->value))
                    throw Error(expr.loc, "cannot emit a non-color string as a Color");
                usesPainter = true;
                return colorLiteralExpr(lit->value);
            }
            return "Firmir::String(\"" + escapeCpp(lit->value) + "\")";
        }
        if (const auto* lit = std::get_if<BoolLit>(&expr.node))
            return lit->value ? "true" : "false";
        if (const auto* ident = std::get_if<IdentExpr>(&expr.node)) {
            if (const auto local = locals.find(ident->name); local != locals.end())
                return ident->name; // handler argument
            return memberAccess(scope.internalName, ident->name, type); // own property
        }
        if (const auto* member = std::get_if<MemberExpr>(&expr.node)) {
            const auto* base = std::get_if<IdentExpr>(&member->object->node);
            if (!base) {
                // childrenRect.x/y/width/height
                return emit(*member->object, ValueType::Rect, scope) + "." +
                       member->name;
            }
            std::string var;
            if (base->name == "parent") {
                if (!scope.parent)
                    throw Error(expr.loc, "internal: 'parent' at the root in codegen");
                var = scope.parent->internalName;
            } else {
                var = base->name;
            }
            return memberAccess(var, member->name, type);
        }
        if (const auto* un = std::get_if<UnaryExpr>(&expr.node)) {
            const ValueType operandType =
                un->op == '!' ? ValueType::Bool : ValueType::Number;
            return std::string("(") + un->op +
                   emit(*un->operand, operandType, scope) + ")";
        }
        if (const auto* bin = std::get_if<BinaryExpr>(&expr.node)) {
            if (bin->op == "%") {
                usesCmath = true;
                return "std::fmod(" + emit(*bin->lhs, ValueType::Number, scope) +
                       ", " + emit(*bin->rhs, ValueType::Number, scope) + ")";
            }
            ValueType operandType = type; // == / !=
            if (bin->op == "&&" || bin->op == "||")
                operandType = ValueType::Bool;
            else if (bin->op == "+" || bin->op == "-" || bin->op == "*" ||
                     bin->op == "/" || bin->op == "<" || bin->op == "<=" ||
                     bin->op == ">" || bin->op == ">=")
                operandType = ValueType::Number;
            return "(" + emit(*bin->lhs, operandType, scope) + " " + bin->op + " " +
                   emit(*bin->rhs, operandType, scope) + ")";
        }
        if (const auto* cond = std::get_if<ConditionalExpr>(&expr.node)) {
            return "(" + emit(*cond->condition, ValueType::Bool, scope) + " ? " +
                   emit(*cond->thenExpr, type, scope) + " : " +
                   emit(*cond->elseExpr, type, scope) + ")";
        }
        if (const auto* call = std::get_if<CallExpr>(&expr.node)) {
            BuiltinFunction function;
            (void)lookupBuiltinFunction(call->callee, function); // checked by sema
            if (function == BuiltinFunction::Min || function == BuiltinFunction::Max) {
                usesAlgorithm = true;
                std::string out = function == BuiltinFunction::Min ? "std::min({"
                                                                   : "std::max({";
                bool first = true;
                for (const Expr& arg : call->args) {
                    if (!first)
                        out += ", ";
                    out += emit(arg, ValueType::Number, scope);
                    first = false;
                }
                return out + "})";
            }
            // lighter/darker: single Color argument
            const std::string arg = emit(call->args[0], ValueType::Color, scope);
            return "(" + arg + (function == BuiltinFunction::Lighter ? ").lighter()"
                                                                     : ").darker()");
        }
        throw Error(expr.loc,
                    "internal: expression cannot be emitted as a static value");
    }

private:
    std::string memberAccess(const std::string& var, const std::string& property,
                             ValueType type) {
        if (type == ValueType::Color)
            usesPainter = true;
        return "Firmir::detail::anyConvert<" + cppValueType(type) + ">(" + var +
               "->getProperty(\"" + property + "\"))";
    }
};

/// Infers the value type of an expression for opaque properties (no static
/// type information available).
ValueType inferType(const Expr& expr) {
    if (std::get_if<NumberLit>(&expr.node))
        return ValueType::Number;
    if (std::get_if<StringLit>(&expr.node))
        return ValueType::String;
    if (std::get_if<BoolLit>(&expr.node))
        return ValueType::Bool;
    if (const auto* cond = std::get_if<ConditionalExpr>(&expr.node))
        return inferType(*cond->thenExpr);
    if (const auto* call = std::get_if<CallExpr>(&expr.node)) {
        BuiltinFunction function;
        if (lookupBuiltinFunction(call->callee, function) &&
            (function == BuiltinFunction::Lighter || function == BuiltinFunction::Darker))
            return ValueType::Color;
        return ValueType::Number;
    }
    if (const auto* bin = std::get_if<BinaryExpr>(&expr.node)) {
        if (bin->op == "&&" || bin->op == "||" || bin->op == "==" ||
            bin->op == "!=" || bin->op == "<" || bin->op == "<=" ||
            bin->op == ">" || bin->op == ">=")
            return ValueType::Bool;
        return ValueType::Number;
    }
    if (const auto* un = std::get_if<UnaryExpr>(&expr.node))
        return un->op == '!' ? ValueType::Bool : ValueType::Number;
    return ValueType::String;
}

class Generator {
public:
    Generator(const File& file, const SemaResult& sema, std::string outputStem)
        : m_file(file), m_sema(sema), m_outputStem(std::move(outputStem)) {}

    GeneratedFile run() {
        const std::string stem = m_outputStem.empty()
                                     ? std::filesystem::path(m_file.fileName).stem().string()
                                     : m_outputStem;
        const std::string base = capitalizeIdentifier(stem);
        const std::string structName = base + "Objects";
        const std::string funcName = "create" + base;

        // The body is emitted first; it collects the include flags.
        std::ostringstream body;
        body << structName << " " << funcName << "(Firmir::Gui::Item* parent) {\n";
        body << "    " << structName << " objects;\n";
        for (const ObjectInfo* obj : m_sema.objects)
            emitObject(*obj, body);
        emitBindings(body);
        emitAnchors(body);
        emitBehaviors(body);
        emitHandlers(body);
        body << "    return objects;\n";
        body << "}\n";

        GeneratedFile out;
        out.headerName = stem + (m_outputStem.empty() ? ".hpp" : ".h");
        out.sourceName = stem + ".cpp";
        out.headerText = buildHeader(structName, funcName);
        out.sourceText = buildSource(out.headerName, body.str());
        return out;
    }

private:
    void emitObject(const ObjectInfo& obj, std::ostringstream& body) {
        const std::string& var = obj.internalName;
        const std::string parentVar =
            obj.parent ? obj.parent->internalName : "parent";
        body << "\n    auto* " << var << " = new " << cppTypeName(obj) << "("
             << parentVar << ");\n";
        if (!obj.parent || !obj.id.empty()) {
            // The root is always exposed; children only when they have an id.
            const std::string field =
                obj.parent ? obj.id : (obj.id.empty() ? "root" : obj.id);
            body << "    objects." << field << " = " << var << ";\n";
        }
        if (!obj.id.empty())
            body << "    " << var << "->setName(\"" << obj.id << "\");\n";

        for (const BindingInfo& binding : m_sema.bindings) {
            if (binding.object != &obj || !binding.isStatic)
                continue;
            const PropertyInfo* prop =
                obj.type ? obj.type->findProperty(binding.property) : nullptr;
            const ValueType type =
                prop ? prop->type : inferType(binding.stmt->value);
            if ((binding.property == "transformOrigin" ||
                 binding.property == "focusPolicy") &&
                std::get_if<IdentExpr>(&binding.stmt->value.node)) {
                const auto* ident = std::get_if<IdentExpr>(&binding.stmt->value.node);
                const std::string enumType = binding.property == "transformOrigin"
                                                 ? "TransformOrigin"
                                                 : "FocusPolicy";
                body << "    " << var << "->setProperty(\"" << binding.property
                     << "\", static_cast<int>(Firmir::Gui::" << enumType << "::"
                     << ident->name << "));\n";
                continue;
            }
            body << "    " << var << "->setProperty(\"" << binding.property
                 << "\", " << m_expr.emit(binding.stmt->value, type, obj) << ");\n";
        }
    }

    /// Emits one bound assignment: the initial set plus a propertyChanged
    /// subscription per dependency. `assignPrefix` is the statement up to the
    /// value expression, e.g. `title->setProperty("x", ` or
    /// `content->setTopMargin(`.
    void emitBoundAssignment(std::ostringstream& body, const ObjectInfo& obj,
                             const Expr& value, ValueType type,
                             const std::vector<std::string>& deps,
                             const std::string& assignPrefix) {
        const std::string& var = obj.internalName;
        const std::string exprText = m_expr.emit(value, type, obj);
        body << "    " << assignPrefix << exprText << ");\n";

        std::vector<std::string> captures{var};
        for (const std::string& dep : deps) {
            const std::string depObj = dep.substr(0, dep.rfind('.'));
            if (std::find(captures.begin(), captures.end(), depObj) == captures.end())
                captures.push_back(depObj);
        }
        std::string captureList;
        for (const std::string& capture : captures) {
            if (!captureList.empty())
                captureList += ", ";
            captureList += capture;
        }
        for (const std::string& dep : deps) {
            const size_t dot = dep.rfind('.');
            body << "    " << dep.substr(0, dot)
                 << "->connect<std::string, std::any>(\n"
                 << "        " << var << ", Firmir::Object::signalPropertyChanged,\n"
                 << "        [" << captureList
                 << "](const std::string& name, const std::any&) {\n"
                 << "            if (name == \"" << dep.substr(dot + 1) << "\")\n"
                 << "                " << assignPrefix << exprText << ");\n"
                 << "        },\n"
                 << "        Firmir::ConnectionMode::Direct);\n";
        }
    }

    void emitBindings(std::ostringstream& body) {
        bool wroteComment = false;
        for (const BindingInfo& binding : m_sema.bindings) {
            if (binding.isStatic)
                continue;
            if (!wroteComment) {
                body << "\n    // bindings\n";
                wroteComment = true;
            }
            const PropertyInfo* prop =
                binding.object->type
                    ? binding.object->type->findProperty(binding.property)
                    : nullptr;
            const ValueType type =
                prop ? prop->type : inferType(binding.stmt->value);
            emitBoundAssignment(body, *binding.object, binding.stmt->value, type,
                                binding.deps,
                                binding.object->internalName + "->setProperty(\"" +
                                    binding.property + "\", ");
        }
    }

    void emitAnchors(std::ostringstream& body) {
        bool wroteComment = false;
        for (const AnchorInfo& anchor : m_sema.anchors) {
            if (!wroteComment) {
                body << "\n    // anchors\n";
                wroteComment = true;
            }
            const std::string& var = anchor.object->internalName;
            const std::string& key = anchor.key;
            if (key == "fill" || key == "centerIn") {
                body << "    " << var << "->" << (key == "fill" ? "fill" : "centerIn")
                     << "(" << targetVar(anchor) << ");\n";
                continue;
            }
            if (key == "leftMargin" || key == "rightMargin" || key == "topMargin" ||
                key == "bottomMargin" || key == "margins" ||
                key == "horizontalCenterOffset" || key == "verticalCenterOffset") {
                if (anchor.deps.empty()) {
                    body << "    " << var << "->" << marginSetterName(key) << "("
                         << m_expr.emit(anchor.stmt->value, ValueType::Number,
                                        *anchor.object)
                         << ");\n";
                } else {
                    emitBoundAssignment(body, *anchor.object, anchor.stmt->value,
                                        ValueType::Number, anchor.deps,
                                        var + "->" + marginSetterName(key) + "(");
                }
                continue;
            }
            // Line anchors (left/right/top/bottom/hCenter/vCenter).
            if (!anchor.target) {
                body << "    " << var << "->" << anchorSetterName(key) << "({});\n";
                continue;
            }
            body << "    " << var << "->" << anchorSetterName(key) << "({"
                 << targetVar(anchor) << ", Firmir::Gui::AnchorLine::"
                 << anchorLineName(anchor.targetLine) << "});\n";
        }
    }

    std::string targetVar(const AnchorInfo& anchor) const {
        if (!anchor.target)
            return "nullptr";
        return anchor.target->internalName;
    }

    static std::string dottedName(const Expr& expr) {
        if (const auto* ident = std::get_if<IdentExpr>(&expr.node))
            return ident->name;
        if (const auto* member = std::get_if<MemberExpr>(&expr.node)) {
            const std::string base = dottedName(*member->object);
            return base.empty() ? std::string() : base + "." + member->name;
        }
        return {};
    }

    static std::string handlerCppType(const std::string& type) {
        if (type == "int") return "int";
        if (type == "double") return "double";
        if (type == "float") return "float";
        if (type == "bool") return "bool";
        if (type == "string") return "Firmir::String";
        if (type == "color") return "Firmir::Gui::Color";
        return type;
    }

    static std::string handlerTemplateTypes(const HandlerStmt& stmt) {
        std::string out;
        for (const HandlerParam& param : stmt.params) {
            if (!out.empty())
                out += ", ";
            out += handlerCppType(param.typeName);
        }
        return out;
    }

    static std::string handlerLambdaParams(const HandlerStmt& stmt) {
        std::string out;
        for (const HandlerParam& param : stmt.params) {
            if (!out.empty())
                out += ", ";
            out += handlerCppType(param.typeName) + " " + param.name;
        }
        return out;
    }

    std::string methodReceiver(const Expr& expr) {
        const std::string name = dottedName(expr);
        if (name == "Firmir.Gui.App" || name == "App") {
            m_usesAppSingleton = true;
            return "Firmir::Object::singleton<Firmir::Gui::App>(\"Firmir.Gui.App\")";
        }
        return name;
    }

    ValueType handlerExprType(const Expr& expr) const {
        if (const auto* ident = std::get_if<IdentExpr>(&expr.node)) {
            if (const auto local = m_expr.locals.find(ident->name);
                local != m_expr.locals.end())
                return local->second;
        }
        return inferType(expr);
    }

    void emitHandlers(std::ostringstream& body) {
        for (const HandlerInfo& handler : m_sema.handlers) {
            const HandlerStmt& stmt = *handler.stmt;
            const std::string& var = handler.object->internalName;
            m_expr.locals.clear();
            for (const HandlerParam& param : stmt.params) {
                ValueType type = ValueType::Number;
                if (param.typeName == "string") type = ValueType::String;
                else if (param.typeName == "bool") type = ValueType::Bool;
                else if (param.typeName == "color") type = ValueType::Color;
                m_expr.locals[param.name] = type;
            }
            body << "\n    // " << stmt.name << "\n";
            if (handler.kind == HandlerKind::PropertyChanged) {
                body << "    " << var
                     << "->connect<std::string, std::any>(" << var
                     << ", Firmir::Object::signalPropertyChanged, [=](const std::string& name, const std::any&) {\n"
                     << "        if (name != \"" << handler.name << "\") return;\n";
            } else {
                const std::string types = handlerTemplateTypes(stmt);
                body << "    " << var << "->connect";
                if (!types.empty())
                    body << "<" << types << ">";
                body << "(" << var << ", \"" << handler.name << "\", [=]("
                     << handlerLambdaParams(stmt) << ") {\n";
            }
            for (const HandlerParam& param : stmt.params)
                body << "        (void)" << param.name << ";\n";
            for (const Expr& command : stmt.commands) {
                const auto& call = std::get<MethodCallExpr>(command.node);
                body << "        " << methodReceiver(*call.object) << "->" << call.method << "(";
                for (std::size_t i = 0; i < call.args.size(); ++i) {
                    if (i)
                        body << ", ";
                    body << m_expr.emit(call.args[i], handlerExprType(call.args[i]),
                                        *handler.object);
                }
                body << ");\n";
            }
            body << "    });\n";
        }
        m_expr.locals.clear();
    }

    void emitBehaviors(std::ostringstream& body) {
        for (const BehaviorInfo& behavior : m_sema.behaviors) {
            body << "\n    // behavior on " << behavior.property << "\n";
            const std::string rootVar = emitAnimationTree(
                body, *behavior.animation, behavior.object->internalName);
            body << "    " << behavior.object->internalName << "->addBehavior(\""
                 << behavior.property << "\", " << rootVar << ");\n";
        }
    }

    /// Emits the construction of one animation subtree and returns its var.
    std::string emitAnimationTree(std::ostringstream& body, const Object& node,
                                  const std::string& parentVar) {
        m_usesAnimation = true;
        const std::string var = "anim_" + std::to_string(m_animationCounter++);
        body << "    auto* " << var << " = new Firmir::Gui::" << node.typeName << "("
             << parentVar << ");\n";
        for (const MemberNode& member : node.members) {
            if (const auto* setting = std::get_if<PropertyStmt>(&member)) {
                if (setting->name == "duration") {
                    // validated by sema: a '<n>ms' literal
                    const auto* number = std::get_if<NumberLit>(&setting->value.node);
                    body << "    " << var
                         << "->setDuration(std::chrono::milliseconds(" << number->text
                         << "));\n";
                } else { // easing — validated identifier
                    const auto* ident = std::get_if<IdentExpr>(&setting->value.node);
                    body << "    " << var << "->setEasing(Firmir::Gui::Easing::"
                         << ident->name << ");\n";
                }
            } else if (const auto* child = std::get_if<ObjectPtr>(&member)) {
                emitAnimationTree(body, **child, var);
            }
        }
        return var;
    }

    std::string buildHeader(const std::string& structName,
                            const std::string& funcName) const {
        std::ostringstream out;
        out << "// Generated by firmlc from \"" << m_file.fileName
            << "\". Do not edit.\n#pragma once\n\n";
        for (const IncludeDirective& inc : m_file.includes) {
            out << "#include "
                << (inc.angled ? "<" + inc.path + ">" : "\"" + inc.path + "\"")
                << "\n";
        }
        out << "\nnamespace Firmir::Gui { class Item; }\n\n";
        out << "/// Pointers into the created item tree (owned by the parent).\n";
        out << "struct " << structName << " {\n";
        for (const ObjectInfo* obj : m_sema.objects) {
            if (obj->parent && obj->id.empty())
                continue;
            const std::string field =
                obj->parent ? obj->id : (obj->id.empty() ? "root" : obj->id);
            out << "    " << cppTypeName(*obj) << "* " << field << " = nullptr;\n";
        }
        out << "};\n\n";
        out << "/// Builds the item tree under @p parent (call before showing the\n"
               "/// window, on the GUI thread).\n";
        out << structName << " " << funcName << "(Firmir::Gui::Item* parent);\n";
        return out.str();
    }

    std::string buildSource(const std::string& headerName, const std::string& body) const {
        std::ostringstream out;
        out << "// Generated by firmlc from \"" << m_file.fileName
            << "\". Do not edit.\n#include \"" << headerName << "\"\n\n";
        out << "#include <any>\n#include <firmir/string.hpp>\n";
        if (m_expr.usesAlgorithm)
            out << "#include <algorithm>\n";
        if (m_expr.usesCmath)
            out << "#include <cmath>\n";
        if (m_expr.usesPainter)
            out << "#include <firmir/gui/painter.hpp>\n";
        if (m_usesAnimation)
            out << "#include <chrono>\n#include <firmir/gui/animation.hpp>\n";
        if (m_usesAppSingleton)
            out << "#include <firmir/gui/app.hpp>\n";
        out << "\n" << body;
        return out.str();
    }

    const File& m_file;
    const SemaResult& m_sema;
    std::string m_outputStem;
    ExprEmitter m_expr;
    int m_animationCounter = 0;
    bool m_usesAnimation = false;
    bool m_usesAppSingleton = false;
};

} // namespace

GeneratedFile generateCode(const File& file, const SemaResult& sema,
                           const std::string& outputStem) {
    return Generator(file, sema, outputStem).run();
}

} // namespace Fml
} // namespace Firmir



