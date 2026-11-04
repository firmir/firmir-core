#include "sema.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace Firmir {
namespace Fml {

namespace {

bool isColorLiteral(const std::string& value) {
    if (value.size() != 7 || value[0] != '#')
        return false;
    for (size_t i = 1; i < value.size(); ++i)
        if (!std::isxdigit(static_cast<unsigned char>(value[i])))
            return false;
    return true;
}

// Anchor key sets (Qt-compatible names).
const std::set<std::string>& lineAnchors() {
    static const std::set<std::string> keys = {"left",           "right",
                                               "top",            "bottom",
                                               "horizontalCenter", "verticalCenter"};
    return keys;
}
const std::set<std::string>& horizontalLines() {
    static const std::set<std::string> keys = {"left", "right", "horizontalCenter"};
    return keys;
}
const std::set<std::string>& verticalLines() {
    static const std::set<std::string> keys = {"top", "bottom", "verticalCenter"};
    return keys;
}
const std::set<std::string>& marginAnchors() {
    static const std::set<std::string> keys = {"leftMargin",   "rightMargin",
                                               "topMargin",    "bottomMargin",
                                               "margins",      "horizontalCenterOffset",
                                               "verticalCenterOffset"};
    return keys;
}
const std::set<std::string>& easingNames() {
    static const std::set<std::string> names = {"Linear",  "InQuad",  "OutQuad",
                                                "InOutQuad", "OutCubic", "OutExpo"};
    return names;
}

void addDep(std::vector<std::string>& deps, const std::string& dep) {
    if (std::find(deps.begin(), deps.end(), dep) == deps.end())
        deps.push_back(dep);
}

// The semantic analyzer. All methods throw Fml::Error on the first error;
// non-fatal findings (opaque types, unused includes) become warnings.
class Analyzer {
public:
    Analyzer(const File& file, const std::vector<std::string>& includePaths,
             SemaResult& result)
        : m_file(file), m_includePaths(includePaths), m_result(result) {}

    void run() {
        resolveIncludes();
        resolveUsings();
        ObjectInfo* root = registerObject(*m_file.root, nullptr);
        processObject(*root);
        detectCycles();
        warnUnusedIncludes();
    }

private:
    // ---------------------------- includes --------------------------------

    void resolveIncludes() {
        const std::filesystem::path fmlDir =
            std::filesystem::path(m_file.fileName).parent_path();
        for (const IncludeDirective& inc : m_file.includes) {
            const std::string shown =
                inc.angled ? "<" + inc.path + ">" : "\"" + inc.path + "\"";
            const std::string resolved = resolveIncludePath(inc, fmlDir);
            if (resolved.empty())
                throw Error(inc.loc, "cannot find include " + shown + searchHint(inc));

            const std::string typeName =
                std::filesystem::path(inc.path).stem().string();
            verifyHeaderDeclaresType(inc, resolved, typeName);

            if (m_types.count(typeName))
                throw Error(inc.loc, "type '" + typeName + "' is included twice");

            if (const TypeInfo* builtin = builtinType(typeName)) {
                m_types[typeName] = builtin;
            } else {
                auto opaque = std::make_unique<TypeInfo>();
                opaque->name = typeName;
                opaque->base = builtinType("Item");
                opaque->opaque = true;
                m_types[typeName] = opaque.get();
                m_result.typeStorage.push_back(std::move(opaque));
            }
            m_includeLocs[typeName] = inc.loc;
        }
    }

    void resolveUsings() {
        for (const UsingDirective& use : m_file.usings) {
            const std::string stem =
                use.typeName.substr(use.typeName.rfind('.') + 1);
            if (!m_types.count(stem))
                throw Error(use.loc, "using '" + use.alias +
                                         "' refers to unknown type '" + use.typeName + "'");
            if (m_types.count(use.alias) || !m_aliases.emplace(use.alias, use.typeName).second)
                throw Error(use.loc, "duplicate type alias '" + use.alias + "'");
        }
    }

    std::string resolveIncludePath(const IncludeDirective& inc,
                                   const std::filesystem::path& fmlDir) const {
        std::vector<std::string> candidates;
        if (!inc.angled)
            candidates.push_back((fmlDir / inc.path).string());
        for (const std::string& dir : m_includePaths)
            candidates.push_back((std::filesystem::path(dir) / inc.path).string());
        for (const std::string& candidate : candidates)
            if (std::filesystem::exists(candidate))
                return candidate;
        return {};
    }

    static std::string searchHint(const IncludeDirective& inc) {
        return inc.angled ? " (searched -I paths)"
                          : " (searched relative to the .fml file and -I paths)";
    }

    static void verifyHeaderDeclaresType(const IncludeDirective& inc,
                                         const std::string& resolved,
                                         const std::string& typeName) {
        std::ifstream in(resolved);
        std::ostringstream buffer;
        buffer << in.rdbuf();
        if (buffer.str().find("class " + typeName) == std::string::npos)
            throw Error(inc.loc, "include '" + inc.path + "' does not declare type '" +
                                     typeName + "' (expected 'class " + typeName + "')");
    }

    // --------------------- pass 1: objects, ids, types --------------------

    ObjectInfo* registerObject(const Object& node, ObjectInfo* parent) {
        // Qualified names (Firmir.Gui.Rectangle) resolve by the last segment:
        // the #include must declare that class.
        std::string typeName = node.typeName;
        if (const auto alias = m_aliases.find(typeName); alias != m_aliases.end())
            typeName = alias->second;
        const size_t dot = typeName.rfind('.');
        const std::string stem =
            dot == std::string::npos ? typeName : typeName.substr(dot + 1);
        const auto typeIt = m_types.find(stem);
        if (typeIt == m_types.end())
            throw Error(node.loc, "unknown type '" + typeName +
                                      "' (missing #include, e.g. #include <firmir/items/" +
                                      stem + ".hpp>)");
        m_usedTypes.insert(stem);

        auto info = std::make_unique<ObjectInfo>();
        info->node = &node;
        info->resolvedTypeName = typeName;
        info->type = typeIt->second;
        info->parent = parent;
        info->declIndex = m_declCounter++;
        info->internalName = "_obj_" + std::to_string(info->declIndex);
        ObjectInfo* result = info.get();
        m_result.objects.push_back(result);
        m_result.objectStorage.push_back(std::move(info));
        if (parent)
            parent->children.push_back(result);

        for (const MemberNode& member : node.members) {
            const auto* prop = std::get_if<PropertyStmt>(&member);
            if (!prop)
                continue;
            if (prop->name == "id") {
                registerId(*result, *prop);
                continue;
            }
            if (!result->propLocs.emplace(prop->name, prop->loc).second)
                throw Error(prop->loc, "property '" + prop->name + "' is assigned twice");
        }
        for (const MemberNode& member : node.members)
            if (const auto* child = std::get_if<ObjectPtr>(&member))
                registerObject(**child, result);
        return result;
    }

    void registerId(ObjectInfo& obj, const PropertyStmt& stmt) {
        if (!obj.id.empty())
            throw Error(stmt.loc, "the object already has id '" + obj.id + "'");
        const auto* ident = std::get_if<IdentExpr>(&stmt.value.node);
        if (!ident)
            throw Error(stmt.loc, "id must be a plain identifier");
        if (ident->name[0] < 'a' || ident->name[0] > 'z')
            throw Error(stmt.loc,
                        "id '" + ident->name + "' must start with a lowercase letter");
        if (!m_result.byId.emplace(ident->name, &obj).second) {
            const SourceLoc& first = m_idLocs[ident->name];
            throw Error(stmt.loc, "duplicate id '" + ident->name + "' (first declared at " +
                                      std::to_string(first.line) + ":" +
                                      std::to_string(first.col) + ")");
        }
        m_idLocs[ident->name] = stmt.loc;
        obj.id = ident->name;
        obj.internalName = ident->name;
        obj.propLocs.emplace("id", stmt.loc);
    }

    // ---------------------- pass 2: members, anchors ----------------------

    void processObject(ObjectInfo& obj) {
        for (const MemberNode& member : obj.node->members) {
            if (const auto* prop = std::get_if<PropertyStmt>(&member)) {
                if (prop->name != "id")
                    processProperty(obj, *prop);
            } else if (const auto* anchor = std::get_if<AnchorStmt>(&member)) {
                processAnchor(obj, *anchor);
            } else if (const auto* behavior = std::get_if<BehaviorStmt>(&member)) {
                processBehavior(obj, *behavior);
            } else if (const auto* handler = std::get_if<HandlerStmt>(&member)) {
                processHandler(obj, *handler);
            }
        }
        checkOverconstrained(obj);
        for (ObjectInfo* child : obj.children)
            processObject(*child);
    }

    void processProperty(ObjectInfo& obj, const PropertyStmt& stmt) {
        const PropertyInfo* prop = obj.type->findProperty(stmt.name);
        if (!prop) {
            if (obj.type->opaque)
                warn(stmt.loc, "cannot verify property '" + stmt.name + "' of opaque type '" +
                                   obj.type->name + "' (no meta information)");
            else
                throw Error(stmt.loc, "unknown property '" + stmt.name + "' of type '" +
                                          obj.type->name + "'");
        }
        if (prop && prop->readOnly)
            throw Error(stmt.loc, "property '" + stmt.name +
                                      "' of type '" + obj.type->name + "' is read-only");
        std::vector<std::string> deps;
        // Enum properties accept their unqualified enumerators in Firml.
        if ((stmt.name == "transformOrigin" || stmt.name == "focusPolicy") &&
            std::get_if<IdentExpr>(&stmt.value.node)) {
            recordBinding(obj, stmt, deps);
            return;
        }
        const ValueType actual = checkExpr(stmt.value, obj, deps);
        if (prop)
            checkAssignable(stmt.loc, stmt.name, prop->type, actual, stmt.value);
        recordBinding(obj, stmt, deps);
    }

    void recordBinding(ObjectInfo& obj, const PropertyStmt& stmt,
                       const std::vector<std::string>& deps) {
        BindingInfo binding;
        binding.loc = stmt.loc;
        binding.object = &obj;
        binding.property = stmt.name;
        binding.stmt = &stmt;
        binding.deps = deps;
        binding.isStatic = deps.empty();
        m_result.bindings.push_back(binding);
        const std::string from = obj.internalName + "." + stmt.name;
        for (const std::string& dep : deps)
            addEdge(from, dep, stmt.loc);
    }

    void checkAssignable(const SourceLoc& loc, const std::string& propName,
                         ValueType expected, ValueType actual, const Expr& value) const {
        if (expected == actual)
            return;
        // A String is accepted where a Color is expected; bare literals are
        // validated against the #RRGGBB form.
        if (expected == ValueType::Color && actual == ValueType::String) {
            if (const auto* lit = std::get_if<StringLit>(&value.node)) {
                if (!isColorLiteral(lit->value))
                    throw Error(loc, "invalid color literal '" + lit->value +
                                         "' (expected #RRGGBB)");
            }
            return;
        }
        throw Error(loc, "cannot assign " + std::string(valueTypeName(actual)) + " to '" +
                             propName + "' of type " + valueTypeName(expected));
    }

    // ------------------------- expression typing --------------------------

    ValueType checkExpr(const Expr& expr, ObjectInfo& scope,
                        std::vector<std::string>& deps) {
        if (std::get_if<NumberLit>(&expr.node))
            return ValueType::Number;
        if (std::get_if<StringLit>(&expr.node))
            return ValueType::String;
        if (std::get_if<BoolLit>(&expr.node))
            return ValueType::Bool;
        if (std::get_if<UndefinedLit>(&expr.node))
            throw Error(expr.loc, "'undefined' is only valid as an anchor value");
        if (const auto* ident = std::get_if<IdentExpr>(&expr.node))
            return checkIdent(*ident, expr.loc, scope, deps);
        if (const auto* member = std::get_if<MemberExpr>(&expr.node))
            return checkMember(*member, expr.loc, scope, deps);
        if (const auto* call = std::get_if<CallExpr>(&expr.node))
            return checkCall(*call, expr.loc, scope, deps);
        if (const auto* unary = std::get_if<UnaryExpr>(&expr.node)) {
            const ValueType operand = checkExpr(*unary->operand, scope, deps);
            if (unary->op == '-') {
                if (operand != ValueType::Number)
                    throw Error(expr.loc, "unary '-' requires a Number operand, got " +
                                              std::string(valueTypeName(operand)));
                return ValueType::Number;
            }
            if (operand != ValueType::Bool)
                throw Error(expr.loc, "unary '!' requires a Bool operand, got " +
                                          std::string(valueTypeName(operand)));
            return ValueType::Bool;
        }
        if (const auto* binary = std::get_if<BinaryExpr>(&expr.node))
            return checkBinary(*binary, expr.loc, scope, deps);
        if (const auto* cond = std::get_if<ConditionalExpr>(&expr.node)) {
            const ValueType condType = checkExpr(*cond->condition, scope, deps);
            if (condType != ValueType::Bool)
                throw Error(expr.loc, "the ternary condition must be Bool, got " +
                                          std::string(valueTypeName(condType)));
            const ValueType thenType = checkExpr(*cond->thenExpr, scope, deps);
            const ValueType elseType = checkExpr(*cond->elseExpr, scope, deps);
            if (thenType != elseType)
                throw Error(expr.loc, "the ternary branches have different types (" +
                                          std::string(valueTypeName(thenType)) + " and " +
                                          valueTypeName(elseType) + ")");
            return thenType;
        }
        if (std::get_if<MethodCallExpr>(&expr.node))
            throw Error(expr.loc, "method calls are only allowed in signal/property handlers");
        throw Error(expr.loc, "unsupported expression");
    }

    ValueType checkIdent(const IdentExpr& ident, const SourceLoc& loc, ObjectInfo& scope,
                         std::vector<std::string>& deps) {
        if (const auto local = m_expressionLocals.find(ident.name);
            local != m_expressionLocals.end())
            return local->second;
        if (ident.name == "parent")
            throw Error(loc, "'parent' must be used as parent.<property>");
        if (const PropertyInfo* prop = scope.type->findProperty(ident.name)) {
            addDep(deps, scope.internalName + "." + ident.name);
            return prop->type;
        }
        if (m_result.byId.count(ident.name))
            throw Error(loc, "unexpected object id '" + ident.name +
                                 "' in an expression (use " + ident.name + ".<property>)");
        throw Error(loc, "unknown identifier '" + ident.name + "'");
    }

    ValueType checkMember(const MemberExpr& member, const SourceLoc& loc,
                          ObjectInfo& scope, std::vector<std::string>& deps) {
        const auto* base = std::get_if<IdentExpr>(&member.object->node);
        if (!base) {
            const ValueType objectType = checkExpr(*member.object, scope, deps);
            if (objectType == ValueType::Rect &&
                (member.name == "x" || member.name == "y" ||
                 member.name == "width" || member.name == "height"))
                return ValueType::Number;
            throw Error(loc, "member access is only supported on ids, 'parent' "
                             "and Rect.x/y/width/height");
        }
        const ObjectInfo* target = nullptr;
        if (base->name == "parent") {
            target = scope.parent;
            if (!target)
                throw Error(loc, "the root object has no 'parent'");
        } else {
            const auto it = m_result.byId.find(base->name);
            if (it == m_result.byId.end()) {
                if (scope.type->findProperty(base->name))
                    throw Error(loc, "'" + base->name +
                                         "' is a property, not an object id (use plain '" +
                                         base->name + "')");
                throw Error(loc, "unknown identifier '" + base->name + "'");
            }
            target = it->second;
        }
        const PropertyInfo* prop = target->type->findProperty(member.name);
        if (!prop)
            throw Error(loc, "unknown property '" + member.name + "' of type '" +
                                 target->type->name + "'");
        addDep(deps, target->internalName + "." + member.name);
        return prop->type;
    }

    ValueType checkCall(const CallExpr& call, const SourceLoc& loc, ObjectInfo& scope,
                        std::vector<std::string>& deps) {
        BuiltinFunction function;
        if (!lookupBuiltinFunction(call.callee, function))
            throw Error(loc, "unknown function '" + call.callee +
                                 "' (supported: min, max, lighter, darker)");
        if (function == BuiltinFunction::Min || function == BuiltinFunction::Max) {
            if (call.args.empty())
                throw Error(loc, "'" + call.callee + "' expects at least 1 argument");
            for (const Expr& arg : call.args) {
                const ValueType argType = checkExpr(arg, scope, deps);
                if (argType != ValueType::Number)
                    throw Error(arg.loc, "'" + call.callee +
                                             "' expects Number arguments, got " +
                                             valueTypeName(argType));
            }
            return ValueType::Number;
        }
        // lighter/darker
        if (call.args.size() != 1)
            throw Error(loc, "'" + call.callee + "' expects exactly 1 argument");
        const ValueType argType = checkExpr(call.args[0], scope, deps);
        if (argType == ValueType::String) {
            if (const auto* lit = std::get_if<StringLit>(&call.args[0].node))
                if (!isColorLiteral(lit->value))
                    throw Error(call.args[0].loc, "invalid color literal '" + lit->value +
                                                      "' (expected #RRGGBB)");
        } else if (argType != ValueType::Color) {
            throw Error(call.args[0].loc, "'" + call.callee +
                                              "' expects a Color argument, got " +
                                              valueTypeName(argType));
        }
        return ValueType::Color;
    }

    ValueType checkBinary(const BinaryExpr& binary, const SourceLoc& loc,
                          ObjectInfo& scope, std::vector<std::string>& deps) {
        const ValueType lhs = checkExpr(*binary.lhs, scope, deps);
        const ValueType rhs = checkExpr(*binary.rhs, scope, deps);
        const std::string& op = binary.op;
        if (op == "&&" || op == "||") {
            if (lhs != ValueType::Bool || rhs != ValueType::Bool)
                throw Error(loc, "operator '" + op + "' requires Bool operands, got " +
                                     std::string(valueTypeName(lhs)) + " and " +
                                     valueTypeName(rhs));
            return ValueType::Bool;
        }
        if (op == "==" || op == "!=") {
            if (lhs != rhs)
                throw Error(loc, "cannot compare " + std::string(valueTypeName(lhs)) +
                                     " and " + valueTypeName(rhs) + " with '" + op + "'");
            return ValueType::Bool;
        }
        const bool arithmetic = op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
        if (lhs != ValueType::Number || rhs != ValueType::Number)
            throw Error(loc, "operator '" + op + "' requires Number operands, got " +
                                 std::string(valueTypeName(lhs)) + " and " +
                                 valueTypeName(rhs));
        return arithmetic ? ValueType::Number : ValueType::Bool;
    }

    // ------------------------------- anchors ------------------------------

    void processAnchor(ObjectInfo& obj, const AnchorStmt& stmt) {
        const std::string& key = stmt.key;
        const bool isLine = lineAnchors().count(key) != 0;
        const bool isMargin = marginAnchors().count(key) != 0;
        if (!isLine && !isMargin && key != "fill" && key != "centerIn")
            throw Error(stmt.loc, "unknown anchor '" + key + "'");
        checkAnchorConflicts(obj, stmt);
        if (!obj.anchorKeys.insert(key).second)
            throw Error(stmt.loc, "anchor '" + key + "' is specified twice");

        AnchorInfo info;
        info.loc = stmt.loc;
        info.object = &obj;
        info.key = key;
        info.stmt = &stmt;
        if (isMargin) {
            resolveMarginAnchor(obj, stmt, info);
        } else if (key == "fill" || key == "centerIn") {
            resolveObjectAnchor(obj, stmt, info);
        } else {
            resolveLineAnchor(obj, stmt, info);
        }
        m_result.anchors.push_back(info);
    }

    void checkAnchorConflicts(const ObjectInfo& obj, const AnchorStmt& stmt) const {
        const std::string& key = stmt.key;
        auto has = [&obj](const char* other) { return obj.anchorKeys.count(other) != 0; };
        auto conflict = [&](const char* other) {
            throw Error(stmt.loc,
                        "anchor '" + key + "' conflicts with 'anchors." + other + "'");
        };
        if (key == "fill" || key == "centerIn") {
            for (const char* other :
                 {"left", "right", "top", "bottom", "horizontalCenter", "verticalCenter"})
                if (has(other))
                    conflict(other);
            if (has("fill"))
                conflict("fill");
            if (has("centerIn"))
                conflict("centerIn");
            return;
        }
        if (horizontalLines().count(key)) {
            if (has("fill"))
                conflict("fill");
            if (has("centerIn"))
                conflict("centerIn");
            if (key != "horizontalCenter" && has("horizontalCenter"))
                conflict("horizontalCenter");
        }
        if (verticalLines().count(key)) {
            if (has("fill"))
                conflict("fill");
            if (has("centerIn"))
                conflict("centerIn");
            if (key != "verticalCenter" && has("verticalCenter"))
                conflict("verticalCenter");
        }
    }

    void checkOverconstrained(const ObjectInfo& obj) const {
        if (obj.anchorKeys.count("left") && obj.anchorKeys.count("right")) {
            const auto it = obj.propLocs.find("width");
            if (it != obj.propLocs.end())
                throw Error(it->second,
                            "anchors 'left' + 'right' conflict with an explicit 'width'");
        }
        if (obj.anchorKeys.count("top") && obj.anchorKeys.count("bottom")) {
            const auto it = obj.propLocs.find("height");
            if (it != obj.propLocs.end())
                throw Error(it->second,
                            "anchors 'top' + 'bottom' conflict with an explicit 'height'");
        }
    }

    void resolveMarginAnchor(ObjectInfo& obj, const AnchorStmt& stmt, AnchorInfo& info) {
        std::vector<std::string> deps;
        const ValueType type = checkExpr(stmt.value, obj, deps);
        if (type != ValueType::Number)
            throw Error(stmt.loc, "anchor '" + stmt.key + "' must be a Number, got " +
                                      std::string(valueTypeName(type)));
        info.deps = deps;
        // Margins shift the geometry of their axis; margins/offsets set both.
        std::vector<std::string> selfProps;
        if (stmt.key == "leftMargin" || stmt.key == "rightMargin" ||
            stmt.key == "horizontalCenterOffset")
            selfProps = {"x", "width"};
        else if (stmt.key == "topMargin" || stmt.key == "bottomMargin" ||
                 stmt.key == "verticalCenterOffset")
            selfProps = {"y", "height"};
        else
            selfProps = {"x", "y", "width", "height"};
        for (const std::string& self : selfProps)
            for (const std::string& dep : deps)
                addEdge(obj.internalName + "." + self, dep, stmt.loc);
    }

    void resolveObjectAnchor(ObjectInfo& obj, const AnchorStmt& stmt, AnchorInfo& info) {
        const Expr& value = stmt.value;
        if (std::get_if<UndefinedLit>(&value.node)) {
            info.targetText = "undefined";
            return;
        }
        const auto* ident = std::get_if<IdentExpr>(&value.node);
        if (!ident)
            throw Error(value.loc, "anchors." + stmt.key +
                                       " target must be 'parent' or an object id");
        info.targetText = ident->name;
        const ObjectInfo* target = resolveAnchorTargetObject(obj, ident->name, value.loc);
        info.target = target;
        if (stmt.key == "fill") {
            static const char* props[] = {"x", "y", "width", "height"};
            for (const char* prop : props) {
                const std::string dep = target->internalName + "." + prop;
                addDep(info.deps, dep);
                addEdge(obj.internalName + "." + prop, dep, stmt.loc);
            }
        } else { // centerIn
            addDep(info.deps, target->internalName + ".x");
            addDep(info.deps, target->internalName + ".width");
            addDep(info.deps, target->internalName + ".y");
            addDep(info.deps, target->internalName + ".height");
            addEdge(obj.internalName + ".x", target->internalName + ".x", stmt.loc);
            addEdge(obj.internalName + ".x", target->internalName + ".width", stmt.loc);
            addEdge(obj.internalName + ".y", target->internalName + ".y", stmt.loc);
            addEdge(obj.internalName + ".y", target->internalName + ".height", stmt.loc);
        }
    }

    void resolveLineAnchor(ObjectInfo& obj, const AnchorStmt& stmt, AnchorInfo& info) {
        const Expr& value = stmt.value;
        if (std::get_if<UndefinedLit>(&value.node)) {
            info.targetText = "undefined"; // explicit anchor reset
            return;
        }
        const auto* member = std::get_if<MemberExpr>(&value.node);
        if (!member)
            throw Error(value.loc, "anchor '" + stmt.key +
                                       "' must reference a line (e.g. parent.left, sibling.right)");
        const auto* base = std::get_if<IdentExpr>(&member->object->node);
        if (!base)
            throw Error(value.loc, "an anchor target must be parent.<line> or <id>.<line>");
        const std::string& line = member->name;
        if (!lineAnchors().count(line))
            throw Error(value.loc, "unknown anchor line '" + line + "'");
        if (horizontalLines().count(stmt.key) && !horizontalLines().count(line))
            throw Error(value.loc, "anchor '" + stmt.key +
                                       "' cannot target the vertical line '" + line + "'");
        if (verticalLines().count(stmt.key) && !verticalLines().count(line))
            throw Error(value.loc, "anchor '" + stmt.key +
                                       "' cannot target the horizontal line '" + line + "'");

        const ObjectInfo* target = resolveAnchorTargetObject(obj, base->name, value.loc);
        info.target = target;
        info.targetLine = line;
        info.targetText = base->name + "." + line;

        // Which target geometry properties this line depends on.
        std::vector<std::string> targetProps;
        if (line == "left")
            targetProps = {"x"};
        else if (line == "right" || line == "horizontalCenter")
            targetProps = {"x", "width"};
        else if (line == "top")
            targetProps = {"y"};
        else
            targetProps = {"y", "height"}; // bottom, verticalCenter

        // Which own geometry properties the anchor sets.
        std::vector<std::string> selfProps;
        if (stmt.key == "left" || stmt.key == "horizontalCenter")
            selfProps = {"x"};
        else if (stmt.key == "right")
            selfProps = {"x", "width"};
        else if (stmt.key == "top" || stmt.key == "verticalCenter")
            selfProps = {"y"};
        else // bottom
            selfProps = {"y", "height"};

        for (const std::string& prop : targetProps)
            addDep(info.deps, target->internalName + "." + prop);
        for (const std::string& self : selfProps)
            for (const std::string& prop : targetProps)
                addEdge(obj.internalName + "." + self, target->internalName + "." + prop,
                        stmt.loc);
    }

    const ObjectInfo* resolveAnchorTargetObject(const ObjectInfo& obj,
                                                const std::string& name,
                                                const SourceLoc& loc) const {
        if (name == "parent") {
            if (!obj.parent)
                throw Error(loc, "the root object has no 'parent' to anchor to");
            return obj.parent;
        }
        const auto it = m_result.byId.find(name);
        if (it == m_result.byId.end())
            throw Error(loc, "unknown anchor target '" + name + "'");
        const ObjectInfo* target = it->second;
        if (target == &obj)
            throw Error(loc, "an object cannot anchor to itself");
        if (target->parent != obj.parent)
            throw Error(loc, "anchor target '" + name + "' is not a sibling of '" +
                                 obj.internalName +
                                 "' (anchors target only the parent or siblings)");
        if (target->declIndex > obj.declIndex)
            throw Error(loc, "anchor target '" + name + "' is declared after '" +
                                 obj.internalName +
                                 "' (sibling anchors require earlier declaration)");
        return target;
    }

    // ------------------------- signal/property handlers -------------------------

    static std::string dottedName(const Expr& expr) {
        if (const auto* ident = std::get_if<IdentExpr>(&expr.node))
            return ident->name;
        if (const auto* member = std::get_if<MemberExpr>(&expr.node)) {
            const std::string base = dottedName(*member->object);
            return base.empty() ? std::string() : base + "." + member->name;
        }
        return {};
    }

    static ValueType handlerParamType(const HandlerParam& param) {
        if (param.typeName == "int" || param.typeName == "double" ||
            param.typeName == "float")
            return ValueType::Number;
        if (param.typeName == "string")
            return ValueType::String;
        if (param.typeName == "bool")
            return ValueType::Bool;
        if (param.typeName == "color")
            return ValueType::Color;
        throw Error(param.loc, "unsupported handler argument type '" +
                                   param.typeName +
                                   "' (supported: int, double, float, string, bool, color)");
    }

    void processHandler(ObjectInfo& obj, const HandlerStmt& stmt) {
        if (stmt.name.size() <= 2)
            throw Error(stmt.loc, "invalid handler name '" + stmt.name + "'");
        std::string target = stmt.name.substr(2);
        HandlerInfo info;
        info.loc = stmt.loc;
        info.object = &obj;
        info.stmt = &stmt;
        const bool propertyChanged =
            target.size() > 7 && target.compare(target.size() - 7, 7, "Changed") == 0;
        if (propertyChanged) {
            info.kind = HandlerKind::PropertyChanged;
            target.resize(target.size() - 7);
            if (!target.empty() && target[0] >= 'A' && target[0] <= 'Z')
                target[0] = static_cast<char>(target[0] - 'A' + 'a');
            if (target.empty() || target[0] < 'a' || target[0] > 'z')
                throw Error(stmt.loc, "the property in '" + stmt.name +
                                          "' must start with a lowercase letter");
            if (!stmt.params.empty())
                throw Error(stmt.loc, "property-change handlers take no arguments");
            if (!obj.type->findProperty(target) && !obj.type->opaque)
                throw Error(stmt.loc, "unknown property '" + target + "' of type '" +
                                          obj.type->name + "'");
        } else {
            info.kind = HandlerKind::Signal;
            if (!target.empty() && target[0] >= 'A' && target[0] <= 'Z')
                target[0] = static_cast<char>(target[0] - 'A' + 'a');
            if (const SignalInfo* signal = obj.type->findSignal(target)) {
                if (signal->parameters.size() != stmt.params.size())
                    throw Error(stmt.loc, "signal '" + target + "' expects " +
                                              std::to_string(signal->parameters.size()) +
                                              " argument(s)");
            }
        }
        info.name = target;

        m_expressionLocals.clear();
        for (std::size_t i = 0; i < stmt.params.size(); ++i) {
            const HandlerParam& param = stmt.params[i];
            if (!m_expressionLocals.emplace(param.name, handlerParamType(param)).second)
                throw Error(param.loc, "duplicate handler argument '" + param.name + "'");
            if (propertyChanged)
                break;
        }
        for (const Expr& command : stmt.commands) {
            const auto* call = std::get_if<MethodCallExpr>(&command.node);
            if (!call)
                throw Error(command.loc,
                            "a handler command must be a method call (id.method(...))");
            const std::string receiver = dottedName(*call->object);
            const TypeInfo* receiverType = nullptr;
            if (const TypeInfo* singleton = builtinSingleton(receiver)) {
                receiverType = singleton;
            } else if (receiver == "parent") {
                if (!obj.parent)
                    throw Error(command.loc, "the root object has no 'parent'");
                receiverType = obj.parent->type;
            } else if (const auto it = m_result.byId.find(receiver);
                       it != m_result.byId.end()) {
                receiverType = it->second->type;
            } else {
                throw Error(command.loc, "unknown method receiver '" + receiver + "'");
            }
            if (const MethodInfo* method = receiverType->findMethod(call->method)) {
                if (method->parameters.size() != call->args.size())
                    throw Error(command.loc, "method '" + call->method + "' expects " +
                                                 std::to_string(method->parameters.size()) +
                                                 " argument(s)");
            } else if (!receiverType->opaque) {
                throw Error(command.loc, "unknown method '" + call->method +
                                             "' of type '" + receiverType->name + "'");
            }
            for (const Expr& arg : call->args) {
                std::vector<std::string> ignoredDeps;
                checkExpr(arg, obj, ignoredDeps);
            }
        }
        m_expressionLocals.clear();
        m_result.handlers.push_back(std::move(info));
    }

    // ------------------------------ behavior ------------------------------

    void processBehavior(ObjectInfo& obj, const BehaviorStmt& stmt) {
        const PropertyInfo* prop = obj.type->findProperty(stmt.property);
        bool knownNumber = false;
        bool knownColor = false;
        if (!prop) {
            if (obj.type->opaque)
                warn(stmt.loc, "cannot verify behavior property '" + stmt.property +
                                   "' of opaque type '" + obj.type->name + "'");
            else
                throw Error(stmt.loc, "unknown property '" + stmt.property +
                                          "' of type '" + obj.type->name + "'");
        } else if (prop->type == ValueType::Number) {
            knownNumber = true;
        } else if (prop->type == ValueType::Color) {
            knownColor = true;
        } else {
            throw Error(stmt.loc, "cannot animate property '" + stmt.property +
                                      "' of type " + valueTypeName(prop->type) +
                                      " (Number or Color only)");
        }
        validateAnimationTree(*stmt.animation, knownNumber, knownColor);

        BehaviorInfo info;
        info.loc = stmt.loc;
        info.object = &obj;
        info.property = stmt.property;
        info.animation = stmt.animation.get();
        m_result.behaviors.push_back(std::move(info));
    }

    /// Validates a behavior animation tree: known animation types, only the
    /// supported settings per leaf, containers hold animations only.
    void validateAnimationTree(const Object& anim, bool knownNumber, bool knownColor) {
        static const std::set<std::string> types = {
            "NumberAnimation", "ColorAnimation", "PauseAnimation",
            "SequentialAnimation", "ParallelAnimation"};
        if (!types.count(anim.typeName))
            throw Error(anim.loc, "unknown animation type '" + anim.typeName +
                                      "' (supported: NumberAnimation, ColorAnimation, "
                                      "PauseAnimation, SequentialAnimation, "
                                      "ParallelAnimation)");
        const bool container = anim.typeName == "SequentialAnimation" ||
                               anim.typeName == "ParallelAnimation";
        // Opaque behavior property (both flags false): settings are still
        // validated below, only the animation/property type match is skipped.
        if (anim.typeName == "NumberAnimation" && knownColor)
            throw Error(anim.loc, "NumberAnimation cannot animate a Color property");
        if (anim.typeName == "ColorAnimation" && knownNumber)
            throw Error(anim.loc, "ColorAnimation cannot animate a Number property");
        if (container) {
            bool empty = true;
            for (const MemberNode& member : anim.members) {
                const auto* child = std::get_if<ObjectPtr>(&member);
                if (!child)
                    throw Error(anim.loc, anim.typeName +
                                              " can only contain animations");
                empty = false;
                validateAnimationTree(**child, knownNumber, knownColor);
            }
            if (empty)
                throw Error(anim.loc, "empty " + anim.typeName);
            return;
        }
        // Leaf animation: duration / easing settings.
        bool haveDuration = false;
        bool haveEasing = false;
        for (const MemberNode& member : anim.members) {
            const auto* setting = std::get_if<PropertyStmt>(&member);
            if (!setting)
                throw Error(anim.loc, anim.typeName +
                                          " cannot contain objects or anchors");
            if (setting->name == "duration") {
                if (haveDuration)
                    throw Error(setting->loc, "duplicate animation setting 'duration'");
                haveDuration = true;
                validateDuration(*setting);
            } else if (setting->name == "easing") {
                if (anim.typeName == "PauseAnimation")
                    throw Error(setting->loc, "PauseAnimation has no easing");
                if (haveEasing)
                    throw Error(setting->loc, "duplicate animation setting 'easing'");
                haveEasing = true;
                validateEasing(*setting);
            } else {
                throw Error(setting->loc,
                            "unknown animation setting '" + setting->name +
                                "' (supported: duration, easing)");
            }
        }
        if (!haveDuration)
            throw Error(anim.loc, anim.typeName + " misses the 'duration' setting");
    }

    static void validateDuration(const PropertyStmt& setting) {
        const auto* number = std::get_if<NumberLit>(&setting.value.node);
        if (!number || number->suffix != "ms")
            throw Error(setting.loc, "the animation duration must be a '<n>ms' literal");
        try {
            (void)std::stoi(number->text);
        } catch (const std::exception&) {
            throw Error(setting.loc, "invalid duration literal '" + number->text + "ms'");
        }
    }

    static void validateEasing(const PropertyStmt& setting) {
        const auto* ident = std::get_if<IdentExpr>(&setting.value.node);
        if (!ident)
            throw Error(setting.loc, "the easing must be an identifier (Linear, ...)");
        if (!easingNames().count(ident->name))
            throw Error(setting.loc, "unknown easing '" + ident->name +
                                         "' (supported: Linear, InQuad, OutQuad, "
                                         "InOutQuad, OutCubic, OutExpo)");
    }

    // --------------------- dependency graph / cycles ----------------------

    void addEdge(const std::string& from, const std::string& to, const SourceLoc& loc) {
        m_result.edges.push_back({from, to});
        m_nodeLocs.emplace(from, loc); // keeps the first writer's location
    }

    void detectCycles() const {
        std::map<std::string, std::vector<std::string>> adjacency;
        for (const DependencyEdge& edge : m_result.edges)
            adjacency[edge.from].push_back(edge.to);

        std::map<std::string, int> color; // 0/absent = white, 1 = gray, 2 = black
        std::vector<std::string> path;
        std::vector<std::pair<std::string, size_t>> stack;

        for (const DependencyEdge& seed : m_result.edges) {
            if (color[seed.from] != 0)
                continue;
            color[seed.from] = 1;
            path.push_back(seed.from);
            stack.emplace_back(seed.from, 0);
            while (!stack.empty()) {
                const std::string node = stack.back().first;
                size_t& index = stack.back().second;
                const auto adjIt = adjacency.find(node);
                const auto* outs = adjIt != adjacency.end() ? &adjIt->second : nullptr;
                if (outs && index < outs->size()) {
                    const std::string next = (*outs)[index++];
                    const int nextColor = color[next];
                    if (nextColor == 1) {
                        // Back edge: `next` is on the current path -> cycle.
                        const auto start = std::find(path.begin(), path.end(), next);
                        std::string message = "dependency cycle detected: ";
                        for (auto it = start; it != path.end(); ++it)
                            message += *it + " -> ";
                        message += next;
                        throw Error(m_nodeLocs.at(node), message);
                    }
                    if (nextColor == 0) {
                        color[next] = 1;
                        path.push_back(next);
                        stack.emplace_back(next, 0);
                    }
                } else {
                    color[node] = 2;
                    stack.pop_back();
                    path.pop_back();
                }
            }
        }
    }

    // ------------------------------ warnings ------------------------------

    void warnUnusedIncludes() {
        for (const IncludeDirective& inc : m_file.includes) {
            const std::string typeName =
                std::filesystem::path(inc.path).stem().string();
            if (!m_usedTypes.count(typeName))
                warn(inc.loc, "unused include '" + inc.path + "' (type '" + typeName +
                                  "' is never used)");
        }
    }

    void warn(const SourceLoc& loc, std::string message) {
        m_result.warnings.push_back(SemaWarning{loc, std::move(message)});
    }

    // ------------------------------- members -------------------------------

    const File& m_file;
    const std::vector<std::string>& m_includePaths;
    SemaResult& m_result;
    std::map<std::string, const TypeInfo*> m_types;      // included type name -> info
    std::map<std::string, SourceLoc> m_includeLocs;      // type name -> include loc
    std::set<std::string> m_usedTypes;
    std::map<std::string, std::string> m_aliases;
    std::map<std::string, ValueType> m_expressionLocals;
    std::map<std::string, SourceLoc> m_idLocs;           // id -> first declaration
    std::map<std::string, SourceLoc> m_nodeLocs;         // dep-graph node -> writer loc
    int m_declCounter = 0;
};

} // namespace

void analyzeFile(const File& file, const std::vector<std::string>& includePaths,
                 SemaResult& result) {
    Analyzer(file, includePaths, result).run();
}

} // namespace Fml
} // namespace Firmir

