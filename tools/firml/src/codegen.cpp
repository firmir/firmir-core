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
    if (obj.type && !obj.type->cppName.empty())
        return obj.type->cppName;
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
    case ValueType::Any: return "Firmir::String"; // runtime-converted
    }
    return "double";
}

/// The dotted name of an identifier/member chain ("Firmir.Gui.App"), or ""
/// for anything else.
std::string dottedExprName(const Expr& expr) {
    if (const auto* ident = std::get_if<IdentExpr>(&expr.node))
        return ident->name;
    if (const auto* member = std::get_if<MemberExpr>(&expr.node)) {
        const std::string base = dottedExprName(*member->object);
        return base.empty() ? std::string() : base + "." + member->name;
    }
    return {};
}

/// The C++ expression evaluating to the given singleton instance.
/// @p canonical is the canonical dotted name (canonicalSingletonName()).
std::string singletonAccess(const std::string& canonical) {
    if (canonical == "Firmir.Gui.Theme")
        return "Firmir::Gui::Theme::instance()";
    if (canonical == "Firmir.Gui.Translator")
        return "Firmir::Gui::Translator::instance()";
    // Firmir.Gui.App is created and registered by the generated window
    // function, so it is looked up dynamically here.
    return "Firmir::Object::singleton<Firmir::Gui::App>(\"Firmir.Gui.App\")";
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
    bool usesTranslator = false; // trId() calls -> translator.hpp include
    std::map<std::string, ValueType> locals;
    const SemaResult* sema = nullptr; // for id/property type resolution

    /// Repeater delegate context, set while emitting inside a delegate loop:
    /// the model variable (empty for a numeric model), the loop index variable
    /// and the model's role table.
    std::string delegateModelVar;
    std::string delegateIndexVar;
    const std::map<std::string, ValueType>* delegateRoles = nullptr;

    /// The Firml type of an expression, re-derived for emission decisions
    /// (string conversion on `+`); sema has already validated the expression.
    ValueType typeOf(const Expr& expr, const ObjectInfo& scope) const {
        if (std::get_if<NumberLit>(&expr.node))
            return ValueType::Number;
        if (std::get_if<StringLit>(&expr.node))
            return ValueType::String;
        if (std::get_if<BoolLit>(&expr.node))
            return ValueType::Bool;
        if (const auto* ident = std::get_if<IdentExpr>(&expr.node)) {
            if (const auto local = locals.find(ident->name); local != locals.end())
                return local->second;
            if (ident->name == "index" && !delegateIndexVar.empty())
                return ValueType::Number;
            if (const PropertyInfo* prop = scope.type->findProperty(ident->name))
                return prop->type;
            if (const CustomPropInfo* custom = findCustomProp(scope, ident->name))
                return custom->type;
            return ValueType::String; // unreachable: sema validated it
        }
        if (const auto* member = std::get_if<MemberExpr>(&expr.node)) {
            const auto* base = std::get_if<IdentExpr>(&member->object->node);
            if (base && base->name == "model" && delegateRoles) {
                const auto role = delegateRoles->find(member->name);
                if (role != delegateRoles->end())
                    return role->second;
                return ValueType::Any; // runtime-filled model (sema warned)
            }
            if (!base) {
                if (const TypeInfo* singleton =
                        builtinSingleton(dottedExprName(*member->object)))
                    return singleton->findProperty(member->name)->type;
                return ValueType::Number; // Rect.x/y/width/height
            }
            if (const TypeInfo* singleton = builtinSingleton(base->name))
                return singleton->findProperty(member->name)->type;
            const ObjectInfo* target =
                base->name == "parent" ? scope.parent : sema->byId.at(base->name);
            if (const PropertyInfo* prop = target->type->findProperty(member->name))
                return prop->type;
            if (const CustomPropInfo* custom = findCustomProp(*target, member->name))
                return custom->type;
            return ValueType::Any; // opaque object's property (sema warned)
        }
        if (const auto* un = std::get_if<UnaryExpr>(&expr.node))
            return un->op == '!' ? ValueType::Bool : ValueType::Number;
        if (const auto* bin = std::get_if<BinaryExpr>(&expr.node)) {
            const std::string& op = bin->op;
            if (op == "&&" || op == "||" || op == "==" || op == "!=" || op == "<" ||
                op == "<=" || op == ">" || op == ">=")
                return ValueType::Bool;
            if (op == "+" && (typeOf(*bin->lhs, scope) == ValueType::String ||
                              typeOf(*bin->rhs, scope) == ValueType::String))
                return ValueType::String;
            return ValueType::Number;
        }
        if (const auto* cond = std::get_if<ConditionalExpr>(&expr.node))
            return typeOf(*cond->thenExpr, scope);
        if (const auto* call = std::get_if<CallExpr>(&expr.node)) {
            BuiltinFunction function;
            if (lookupBuiltinFunction(call->callee, function)) {
                if (function == BuiltinFunction::Lighter ||
                    function == BuiltinFunction::Darker)
                    return ValueType::Color;
                if (function == BuiltinFunction::TrId)
                    return ValueType::String;
            }
            return ValueType::Number;
        }
        return ValueType::Number;
    }

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
            if (ident->name == "index" && !delegateIndexVar.empty())
                return "(double)" + delegateIndexVar; // delegate row index
            return memberAccess(scope.internalName, ident->name, type); // own property
        }
        if (const auto* member = std::get_if<MemberExpr>(&expr.node)) {
            const auto* base = std::get_if<IdentExpr>(&member->object->node);
            if (base && base->name == "model" && delegateRoles) {
                // Delegate role access: model.<role> -> model->data(index, "role").
                return "Firmir::detail::anyConvert<" + cppValueType(type) + ">(" +
                       delegateModelVar + "->data(" + delegateIndexVar + ", \"" +
                       member->name + "\"))";
            }
            if (!base) {
                // A dotted singleton reference: Firmir.Gui.App.screenWidth.
                const std::string dotted = dottedExprName(*member->object);
                if (builtinSingleton(dotted))
                    return memberAccess(singletonAccess(canonicalSingletonName(dotted)),
                                        member->name, type);
                // childrenRect.x/y/width/height
                return emit(*member->object, ValueType::Rect, scope) + "." +
                       member->name;
            }
            // The short singleton form: App.screenWidth / Theme.paddingSmall.
            if (builtinSingleton(base->name))
                return memberAccess(singletonAccess(canonicalSingletonName(base->name)),
                                    member->name, type);
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
            if (bin->op == "+" && typeOf(expr, scope) == ValueType::String) {
                // String concatenation (sema allows String/Number operands);
                // Number sides are formatted with Firmir::String::number().
                return "(" +
                       emitStringOperand(*bin->lhs, typeOf(*bin->lhs, scope), scope) +
                       " + " +
                       emitStringOperand(*bin->rhs, typeOf(*bin->rhs, scope), scope) +
                       ")";
            }
            if (bin->op == "%") {
                usesCmath = true;
                return "std::fmod(" + emit(*bin->lhs, ValueType::Number, scope) +
                       ", " + emit(*bin->rhs, ValueType::Number, scope) + ")";
            }
            // == / != compare values of the operands' own type, not the
            // context type (the result is Bool, the operands usually are
            // not). Any (opaque-object property) takes the other side's
            // type; Any/Any falls back to Number.
            ValueType operandType = typeOf(*bin->lhs, scope); // == / !=
            if (operandType == ValueType::Any)
                operandType = typeOf(*bin->rhs, scope);
            if (operandType == ValueType::Any)
                operandType = ValueType::Number;
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
            if (function == BuiltinFunction::TrId) {
                usesTranslator = true;
                return "Firmir::Gui::Translator::instance()->trId(" +
                       emit(call->args[0], ValueType::String, scope) + ")";
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
    /// Emits one operand of a string `+`: Number values are converted with
    /// Firmir::String::number(), everything else is emitted as a String.
    std::string emitStringOperand(const Expr& expr, ValueType actual,
                                  const ObjectInfo& scope) {
        if (actual == ValueType::Number)
            return "Firmir::String::number(" + emit(expr, ValueType::Number, scope) +
                   ")";
        return emit(expr, ValueType::String, scope);
    }

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
        if (bin->op == "+" && (inferType(*bin->lhs) == ValueType::String ||
                               inferType(*bin->rhs) == ValueType::String))
            return ValueType::String;
        return ValueType::Number;
    }
    if (const auto* un = std::get_if<UnaryExpr>(&expr.node))
        return un->op == '!' ? ValueType::Bool : ValueType::Number;
    return ValueType::String;
}

class Generator {
public:
    Generator(const File& file, const SemaResult& sema, std::string outputStem)
        : m_file(file), m_sema(sema), m_outputStem(std::move(outputStem)) {
        m_expr.sema = &sema;
    }

    /// The declared value type of a property: builtin metadata first, then
    /// script-declared (`property`) dynamic properties, then (opaque types)
    /// expression inference.
    ValueType propValueType(const ObjectInfo& obj, const std::string& name,
                            const Expr& fallback) const {
        if (obj.type)
            if (const PropertyInfo* prop = obj.type->findProperty(name))
                return prop->type;
        if (const CustomPropInfo* custom = findCustomProp(obj, name))
            return custom->type;
        return inferType(fallback);
    }

    /// The C++ default value for a script-declared property without an
    /// initializer: 0 / "" / false / opaque black.
    std::string defaultValueExpr(ValueType type) {
        switch (type) {
        case ValueType::Bool: return "false";
        case ValueType::String: return "Firmir::String()";
        case ValueType::Color:
            m_expr.usesPainter = true;
            return "Firmir::Gui::Color()";
        case ValueType::Rect: return "Firmir::Gui::RectF()";
        case ValueType::Any: return "Firmir::String()";
        case ValueType::Number: break;
        }
        return "0.0";
    }

    GeneratedFile run() {
        const std::string stem = m_outputStem.empty()
                                     ? std::filesystem::path(m_file.fileName).stem().string()
                                     : m_outputStem;
        const std::string base = capitalizeIdentifier(stem);
        const std::string structName = base + "Objects";
        const std::string funcName = "create" + base;

        // The body is emitted first; it collects the include flags.
        const ObjectInfo* rootInfo = m_sema.objects.front();
        const bool windowRoot = rootInfo->type->kind == TypeKind::Window;
        std::ostringstream body;
        if (windowRoot)
            body << structName << " " << funcName << "(Firmir::Gui::Screen* screen) {\n";
        else
            body << structName << " " << funcName << "(Firmir::Gui::Item* parent) {\n";
        body << "    " << structName << " objects;\n";
        for (const ObjectInfo* obj : m_sema.objects) {
            // Delegate subtrees are emitted by their Repeater (inside the
            // instantiation loop); ListElements compile into append() calls
            // of their ListModel; Connections/Binding are declarative — sema
            // already turned them into handler/binding table entries.
            if (obj->delegateScope || obj->type->kind == TypeKind::ModelElement ||
                obj->type->kind == TypeKind::Connections ||
                obj->type->kind == TypeKind::Binding)
                continue;
            emitObject(*obj, body);
        }
        if (windowRoot && m_sema.usesAppSingleton) {
            // The singleton must exist before the bindings below evaluate it.
            body << "\n    auto* app = new Firmir::Gui::App(" << rootInfo->internalName
                 << ", " << rootInfo->internalName << ");\n";
            body << "    Firmir::Object::registerSingleton(\"Firmir.Gui.App\", app);\n";
            body << "    objects.app = app;\n";
        }
        emitBindings(body);
        emitAnchors(body);
        emitBehaviors(body);
        emitHandlers(body);
        // Delegate instantiation loops run last: their expressions may read
        // custom properties of outer objects, which the bindings pass above
        // has initialized by then.
        for (const ObjectInfo* repeater : m_repeaters)
            emitDelegateLoop(*repeater, body);
        if (windowRoot)
            body << "\n    " << rootInfo->internalName << "->show();\n";
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
        std::string parentVar;
        if (!obj.parent)
            parentVar = obj.type->kind == TypeKind::Window ? "screen" : "parent";
        else if (obj.parent->type->kind == TypeKind::Window ||
                 obj.parent->type->contentItemParent)
            parentVar = obj.parent->internalName + "->contentItem()";
        else
            parentVar = obj.parent->internalName;
        body << "\n    auto* " << var << " = new " << cppTypeName(obj) << "("
             << parentVar << ");\n";
        // Containers with imperative child management (StackView::push,
        // SwipeView/SplitView::addItem) adopt each declared child.
        if (obj.parent && !obj.parent->type->childAddMethod.empty() &&
            obj.type->kind == TypeKind::Item) {
            body << "    " << obj.parent->internalName << "->"
                 << obj.parent->type->childAddMethod << "(" << var << ");\n";
        }
        if (!obj.parent || !obj.id.empty()) {
            // The root is always exposed; children only when they have an id.
            const std::string field =
                obj.parent ? obj.id : (obj.id.empty() ? "root" : obj.id);
            body << "    objects." << field << " = " << var << ";\n";
        }
        if (!obj.id.empty())
            body << "    " << var << "->setName(\"" << obj.id << "\");\n";

        // Script-declared properties without an initializer get the type
        // default; initialized ones are emitted as static/bound assignments.
        for (const CustomPropInfo& custom : obj.customProps) {
            if (custom.decl->hasValue)
                continue;
            body << "    " << var << "->setProperty(\"" << custom.decl->name
                 << "\", " << defaultValueExpr(custom.type) << ");\n";
        }

        if (obj.type->kind == TypeKind::Timer) {
            emitTimerSettings(obj, body);
            return;
        }
        if (obj.type->kind == TypeKind::Model) {
            emitListModelRows(obj, body);
            return;
        }
        for (const BindingInfo& binding : m_sema.bindings) {
            if (binding.object != &obj || !binding.isStatic)
                continue;
            const ValueType type =
                propValueType(obj, binding.property, binding.stmt->value);
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
        if (obj.type == builtinType("Repeater"))
            m_repeaters.push_back(&obj); // the delegate loop is emitted last
    }

    /// Emits the ListElement children of a ListModel as append() calls. Sema
    /// guarantees literal role values; color-looking strings become Color.
    void emitListModelRows(const ObjectInfo& obj, std::ostringstream& body) {
        const std::string& var = obj.internalName;
        for (const ObjectInfo* child : obj.children) {
            body << "    " << var << "->append({";
            bool first = true;
            for (const MemberNode& member : child->node->members) {
                const auto& prop = std::get<PropertyStmt>(member);
                if (!first)
                    body << ", ";
                first = false;
                body << "{\"" << prop.name << "\", " << roleValueExpr(prop) << "}";
            }
            body << "});\n";
        }
    }

    /// The C++ expression for one ListElement role value (std::any element).
    std::string roleValueExpr(const PropertyStmt& prop) {
        if (const auto* lit = std::get_if<NumberLit>(&prop.value.node))
            return numberLiteral(*lit, prop.loc);
        if (const auto* unary = std::get_if<UnaryExpr>(&prop.value.node)) {
            // A signed number literal (-1 / +2), validated by sema.
            const auto& lit = std::get<NumberLit>(unary->operand->node);
            return "(" + std::string(1, unary->op) + numberLiteral(lit, prop.loc) + ")";
        }
        if (const auto* lit = std::get_if<BoolLit>(&prop.value.node))
            return lit->value ? "true" : "false";
        const auto& lit = std::get<StringLit>(prop.value.node); // checked by sema
        if (isColorLiteralText(lit.value)) {
            m_expr.usesPainter = true;
            return colorLiteralExpr(lit.value);
        }
        return "Firmir::String(\"" + escapeCpp(lit.value) + "\")";
    }

    /// Emits the delegate instantiation loop of a Repeater: one delegate
    /// subtree per model row, with `model.<role>`/`index` in scope.
    ///
    /// A Repeater bound to a ListModel object (as opposed to a numeric
    /// literal model) gets a rebuildable delegate set: the delegates are
    /// re-created whenever the model's `count` property changes. The rebuild
    /// is scheduled through the event loop (Timer::singleShot) so a model
    /// mutation triggered from a delegate's own handler (e.g. a delete
    /// button removing its row) never deletes the emitting item mid-signal.
    /// Consecutive count changes within one event-loop turn coalesce into a
    /// single rebuild: filling a model row by row must not produce a
    /// delete+recreate cycle (a visible flicker) per row.
    void emitDelegateLoop(const ObjectInfo& obj, std::ostringstream& body) {
        const std::string base = std::to_string(obj.declIndex);
        const std::string indexVar = "_index_" + base;
        const std::string repeaterVar = obj.internalName;
        std::string modelVar;
        body << "    {\n";
        if (obj.modelObject) {
            m_usesDynamicRepeater = true;
            modelVar = "_model_" + base;
            const std::string rebuildVar = "_rebuild_" + base;
            const std::string pendingVar = "_rebuild_pending_" + base;
            body << "        auto* " << modelVar << " = "
                 << obj.modelObject->internalName << ";\n";
            body << "        auto " << rebuildVar
                 << " = std::make_shared<std::function<void()>>();\n";
            body << "        auto " << pendingVar
                 << " = std::make_shared<bool>(false);\n";
            body << "        *" << rebuildVar << " = [=]() {\n";
            // Drop the delegates of the previous generation (all the
            // Repeater's children are delegate instances).
            body << "            const std::vector<Firmir::Gui::Item*> _old_"
                 << base << " = " << repeaterVar << "->childItems();\n";
            body << "            for (Firmir::Gui::Item* _child_" << base
                 << " : _old_" << base << ")\n";
            body << "                delete _child_" << base << ";\n";
        }
        const std::string countExpr =
            obj.modelObject ? modelVar + "->count()" : std::to_string(obj.modelCount);
        body << "        for (int " << indexVar << " = 0; " << indexVar << " < "
             << countExpr << "; ++" << indexVar << ") {\n";

        m_expr.delegateModelVar = modelVar;
        m_expr.delegateIndexVar = indexVar;
        m_expr.delegateRoles = obj.modelObject ? &obj.modelObject->roles : nullptr;
        for (const ObjectInfo* sub : m_sema.objects)
            if (sub->delegateScope == &obj)
                emitObject(*sub, body);
        emitBindings(body, &obj);
        emitHandlers(body, &obj);
        m_expr.delegateModelVar.clear();
        m_expr.delegateIndexVar.clear();
        m_expr.delegateRoles = nullptr;

        body << "        }\n";
        if (obj.modelObject) {
            const std::string rebuildVar = "_rebuild_" + base;
            const std::string pendingVar = "_rebuild_pending_" + base;
            body << "        };\n";
            body << "        (*" << rebuildVar << ")();\n";
            body << "        " << modelVar << "->connect<std::string, std::any>(\n";
            body << "            " << repeaterVar
                 << ", Firmir::Object::signalPropertyChanged,\n";
            body << "            [" << rebuildVar << ", " << pendingVar << ", "
                 << repeaterVar
                 << "](const std::string& name, const std::any&) {\n";
            // Timer requires a positive interval; 1ms is effectively
            // immediate but still deferred to the next loop iteration. A
            // burst of count changes (e.g. filling the model row by row)
            // schedules only the first rebuild — the rest are redundant.
            body << "                if (name == \"count\" && !*"
                 << pendingVar << ") {\n";
            body << "                    *" << pendingVar << " = true;\n";
            body << "                    Firmir::Timer::singleShot(\n";
            body << "                        std::chrono::milliseconds(1), "
                 << repeaterVar << ",\n";
            body << "                        [" << rebuildVar << ", "
                 << pendingVar << "] {\n";
            body << "                            *" << pendingVar
                 << " = false;\n";
            body << "                            (*" << rebuildVar
                 << ")();\n                        });\n";
            body << "                }\n";
            body << "            },\n";
            body << "            Firmir::ConnectionMode::Direct);\n";
        }
        body << "    }\n";
    }

    /// Timer settings are applied through the native Firmir::Timer API (no
    /// registered properties); `running` is applied last, after interval and
    /// singleShot. Sema guarantees the values are static.
    void emitTimerSettings(const ObjectInfo& obj, std::ostringstream& body) {
        m_usesTimer = true;
        const BindingInfo* running = nullptr;
        for (const BindingInfo& binding : m_sema.bindings) {
            if (binding.object != &obj || !binding.isStatic)
                continue;
            if (binding.property == "interval") {
                body << "    " << obj.internalName
                     << "->setInterval(std::chrono::milliseconds(static_cast<long long>("
                     << m_expr.emit(binding.stmt->value, ValueType::Number, obj)
                     << ")));\n";
            } else if (binding.property == "singleShot") {
                body << "    " << obj.internalName << "->setSingleShot("
                     << m_expr.emit(binding.stmt->value, ValueType::Bool, obj) << ");\n";
            } else if (binding.property == "running") {
                running = &binding;
            }
        }
        if (running) {
            const auto* lit = std::get_if<BoolLit>(&running->stmt->value.node);
            if (lit && !lit->value)
                return; // running: false is the default
            if (lit) {
                body << "    " << obj.internalName << "->start();\n";
                return;
            }
            body << "    if ("
                 << m_expr.emit(running->stmt->value, ValueType::Bool, obj) << ")\n"
                 << "        " << obj.internalName << "->start();\n";
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
        // Inside a delegate loop the re-evaluation lambda also needs the
        // loop variables (model / index) the expression may reference.
        if (!m_expr.delegateIndexVar.empty()) {
            captures.push_back(m_expr.delegateIndexVar);
            if (!m_expr.delegateModelVar.empty())
                captures.push_back(m_expr.delegateModelVar);
        }
        for (const std::string& dep : deps) {
            const std::string depObj = dep.substr(0, dep.rfind('.'));
            if (builtinSingleton(depObj))
                continue; // singletons are accessed inline; nothing to capture
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
            const std::string depObj = dep.substr(0, dot);
            body << "    "
                 << (builtinSingleton(depObj) ? singletonAccess(depObj) : depObj)
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

    /// `delegate` selects the emission scope: nullptr for top-level objects,
    /// or the Repeater whose delegate subtree is being emitted in its loop.
    void emitBindings(std::ostringstream& body, const ObjectInfo* delegate = nullptr) {
        bool wroteComment = false;
        for (const BindingInfo& binding : m_sema.bindings) {
            if (binding.isStatic)
                continue;
            if (binding.object->delegateScope != delegate)
                continue;
            if (!wroteComment) {
                body << "\n    // bindings\n";
                wroteComment = true;
            }
            const ValueType type = propValueType(*binding.object, binding.property,
                                                 binding.stmt->value);
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
        // Anchors reference items; a Window target means its content item.
        if (anchor.target->type->kind == TypeKind::Window)
            return anchor.target->internalName + "->contentItem()";
        return anchor.target->internalName;
    }

    static std::string dottedName(const Expr& expr) { return dottedExprName(expr); }

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
        if (const std::string canonical = canonicalSingletonName(name);
            !canonical.empty()) {
            if (canonical == "Firmir.Gui.App")
                m_usesAppSingleton = true;
            else if (canonical == "Firmir.Gui.Theme")
                m_usesThemeSingleton = true;
            return singletonAccess(canonical);
        }
        return name;
    }

    ValueType handlerExprType(const Expr& expr, const ObjectInfo& scope) {
        // Locals (handler parameters) shadow everything; index is a Number.
        if (const auto* ident = std::get_if<IdentExpr>(&expr.node)) {
            if (const auto local = m_expr.locals.find(ident->name);
                local != m_expr.locals.end())
                return local->second;
        }
        // Everything else resolves like a binding expression: own/builtin
        // properties, model roles, opaque properties (Any).
        return m_expr.typeOf(expr, scope);
    }

    /// Emits a `<target> = <value>` handler command as a property write; the
    /// write goes through setProperty(), so behaviors on the target property
    /// animate the change. The target shape was validated by sema.
    void emitAssignCommand(std::ostringstream& body, const ObjectInfo& scope,
                           const AssignExpr& assign) {
        const ObjectInfo* targetObj = &scope;
        std::string propName;
        std::string singletonReceiver; // non-empty for `Theme.x = ...`
        if (const auto* ident = std::get_if<IdentExpr>(&assign.target->node)) {
            propName = ident->name;
        } else {
            const auto& member = std::get<MemberExpr>(assign.target->node);
            const std::string baseName = dottedName(*member.object);
            if (const std::string canonical = canonicalSingletonName(baseName);
                !canonical.empty()) {
                singletonReceiver = singletonAccess(canonical);
                if (canonical == "Firmir.Gui.Theme")
                    m_usesThemeSingleton = true;
                else if (canonical == "Firmir.Gui.App")
                    m_usesAppSingleton = true;
            } else {
                const auto& base = std::get<IdentExpr>(member.object->node);
                targetObj =
                    base.name == "parent" ? scope.parent : m_sema.byId.at(base.name);
            }
            propName = member.name;
        }
        if (!singletonReceiver.empty()) {
            const PropertyInfo* prop =
                builtinSingleton(canonicalSingletonName(
                    dottedName(*std::get<MemberExpr>(assign.target->node).object)))
                    ->findProperty(propName);
            body << "        " << singletonReceiver << "->setProperty(\""
                 << propName << "\", "
                 << m_expr.emit(*assign.value, prop ? prop->type : ValueType::Any,
                                scope)
                 << ");\n";
            return;
        }
        ValueType type = propValueType(*targetObj, propName, *assign.value);
        if (targetObj->type->opaque && !targetObj->type->findProperty(propName) &&
            !findCustomProp(*targetObj, propName)) {
            // Opaque target: the property type is unknown statically, so emit
            // the value with its own type; the runtime setProperty() converts.
            type = m_expr.typeOf(*assign.value, scope);
        }
        body << "        " << targetObj->internalName << "->setProperty(\""
             << propName << "\", " << m_expr.emit(*assign.value, type, scope)
             << ");\n";
    }

    /// `delegate` selects the emission scope (see emitBindings).
    void emitHandlers(std::ostringstream& body, const ObjectInfo* delegate = nullptr) {
        for (const HandlerInfo& handler : m_sema.handlers) {
            if (handler.object->delegateScope != delegate)
                continue;
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
            // The command scope differs from the signal owner for Connections.
            const ObjectInfo& scope =
                handler.context ? *handler.context : *handler.object;
            for (const Expr& command : stmt.commands) {
                if (const auto* assign = std::get_if<AssignExpr>(&command.node)) {
                    emitAssignCommand(body, scope, *assign);
                    continue;
                }
                const auto& call = std::get<MethodCallExpr>(command.node);
                body << "        " << methodReceiver(*call.object) << "->" << call.method << "(";
                for (std::size_t i = 0; i < call.args.size(); ++i) {
                    if (i)
                        body << ", ";
                    body << m_expr.emit(call.args[i], handlerExprType(call.args[i], scope),
                                        scope);
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
        const bool windowRoot =
            m_sema.objects.front()->type->kind == TypeKind::Window;
        out << "\nnamespace Firmir::Gui { class Item;";
        if (windowRoot)
            out << " class Screen;";
        if (windowRoot && m_sema.usesAppSingleton)
            out << " class App;";
        out << " }\n\n";
        out << (windowRoot
                    ? "/// Pointers into the created window and object tree (owned\n"
                      "/// by the window).\n"
                    : "/// Pointers into the created item tree (owned by the parent).\n");
        out << "struct " << structName << " {\n";
        for (const ObjectInfo* obj : m_sema.objects) {
            if (obj->parent && obj->id.empty())
                continue;
            const std::string field =
                obj->parent ? obj->id : (obj->id.empty() ? "root" : obj->id);
            out << "    " << cppTypeName(*obj) << "* " << field << " = nullptr;\n";
        }
        if (windowRoot && m_sema.usesAppSingleton)
            out << "    Firmir::Gui::App* app = nullptr;\n";
        out << "};\n\n";
        if (windowRoot) {
            out << "/// Creates the window on @p screen, builds the object tree and\n"
                   "/// shows the window (call on the GUI thread).\n";
            out << structName << " " << funcName << "(Firmir::Gui::Screen* screen);\n";
        } else {
            out << "/// Builds the item tree under @p parent (call before showing the\n"
                   "/// window, on the GUI thread).\n";
            out << structName << " " << funcName << "(Firmir::Gui::Item* parent);\n";
        }
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
        else if (m_usesTimer)
            out << "#include <chrono>\n";
        if (m_usesDynamicRepeater) {
            if (!m_usesAnimation && !m_usesTimer)
                out << "#include <chrono>\n";
            out << "#include <functional>\n#include <memory>\n#include <vector>\n";
            out << "#include <firmir/gui/listmodel.hpp>\n#include <firmir/timer.hpp>\n";
        }
        if (m_usesAppSingleton || m_sema.usesAppSingleton)
            out << "#include <firmir/gui/app.hpp>\n";
        if (m_usesThemeSingleton || m_sema.usesThemeSingleton)
            out << "#include <firmir/gui/theme.hpp>\n";
        if (m_expr.usesTranslator || m_usesTranslatorSingleton ||
            m_sema.usesTranslatorSingleton)
            out << "#include <firmir/gui/translator.hpp>\n";
        out << "\n" << body;
        return out.str();
    }

    const File& m_file;
    const SemaResult& m_sema;
    std::string m_outputStem;
    ExprEmitter m_expr;
    std::vector<const ObjectInfo*> m_repeaters; // delegate loops pending emission
    /// True when some Repeater is bound to a ListModel and its delegates
    /// must be rebuilt when the model count changes (needs <functional>,
    /// <memory>, <vector>, <chrono> and <firmir/timer.hpp>).
    bool m_usesDynamicRepeater = false;
    int m_animationCounter = 0;
    bool m_usesAnimation = false;
    bool m_usesTimer = false;
    bool m_usesAppSingleton = false;
    bool m_usesThemeSingleton = false;
    bool m_usesTranslatorSingleton = false;
};

} // namespace

GeneratedFile generateCode(const File& file, const SemaResult& sema,
                           const std::string& outputStem) {
    return Generator(file, sema, outputStem).run();
}

} // namespace Fml
} // namespace Firmir



