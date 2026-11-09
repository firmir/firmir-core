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

// Anchor key sets.
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
        // Language constructs without a runtime class/header of their own
        // (codegen inlines them); available in every file without an include.
        m_types["Connections"] = builtinType("Connections");
        m_types["Binding"] = builtinType("Binding");
        m_types["Component"] = builtinType("Component");
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
        if (info->type->kind == TypeKind::Window && parent)
            throw Error(node.loc, "'Window' is only allowed as the root object");
        if (!parent && (info->type->kind == TypeKind::Connections ||
                        info->type->kind == TypeKind::Binding ||
                        info->type->kind == TypeKind::Component))
            throw Error(node.loc, "'" + info->type->name +
                                      "' is only allowed as a child object");
        if (parent) {
            const TypeKind parentKind = parent->type->kind;
            if (parentKind == TypeKind::Timer || parentKind == TypeKind::ModelElement ||
                parentKind == TypeKind::Connections || parentKind == TypeKind::Binding)
                throw Error(node.loc, "'" + parent->type->name +
                                          "' cannot contain objects");
            if (parentKind == TypeKind::Model &&
                info->type->kind != TypeKind::ModelElement)
                throw Error(node.loc,
                            "'ListModel' can only contain ListElement objects");
            if (info->type->kind == TypeKind::ModelElement &&
                parentKind != TypeKind::Model)
                throw Error(node.loc,
                            "'ListElement' is only allowed inside a ListModel");
            // The direct child of a Repeater is its delegate template; deeper
            // objects inherit the delegate scope.
            if (parent->type == builtinType("Repeater"))
                info->delegateScope = parent;
            else
                info->delegateScope = parent->delegateScope;
            if (info->delegateScope && info->type == builtinType("Repeater"))
                throw Error(node.loc, "a Repeater cannot be nested inside a delegate");
            if (info->delegateScope && info->type->kind == TypeKind::Model)
                throw Error(node.loc,
                            "a ListModel cannot be declared inside a delegate");
            if (info->delegateScope &&
                (info->type->kind == TypeKind::Connections ||
                 info->type->kind == TypeKind::Binding))
                throw Error(node.loc, "'" + info->type->name +
                                           "' is not supported inside a Repeater delegate yet");
            // The direct child of a Component is its template root; deeper
            // objects inherit the component scope.
            if (parent->type->kind == TypeKind::Component)
                info->componentScope = parent;
            else
                info->componentScope = parent->componentScope;
            if (info->type->kind == TypeKind::Component) {
                if (info->componentScope)
                    throw Error(node.loc, "a Component cannot be nested inside "
                                          "another Component");
                if (info->delegateScope)
                    throw Error(node.loc, "a Component is not supported inside "
                                          "a Repeater delegate");
            }
            if (info->componentScope && info->type == builtinType("Repeater"))
                throw Error(node.loc, "a Repeater is not supported inside a "
                                      "Component yet");
        }
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
        // Function signatures are registered in pass 1 (after the id loop, so
        // the receiver check sees the id wherever it is declared); bodies are
        // checked lazily in pass 2 (checkFunctionBody).
        for (const MemberNode& member : node.members)
            if (const auto* fn = std::get_if<FunctionDecl>(&member))
                registerFunction(*result, *fn);
        for (const MemberNode& member : node.members)
            if (const auto* child = std::get_if<ObjectPtr>(&member))
                registerObject(**child, result);
        if (result->type->kind == TypeKind::Model)
            collectModelRoles(*result);
        return result;
    }

    /// Fills the role table of a ListModel from its ListElement children.
    /// Runs in pass 1 so `model.<role>` checks work regardless of the
    /// declaration order of the model and the repeater referencing it.
    void collectModelRoles(ObjectInfo& obj) {
        for (const ObjectInfo* child : obj.children) {
            for (const MemberNode& member : child->node->members) {
                const auto* prop = std::get_if<PropertyStmt>(&member);
                if (!prop)
                    throw Error(child->node->loc,
                                "'ListElement' can only contain role assignments");
                if (prop->name == "id")
                    continue; // already rejected by registerId
                ValueType type;
                if (std::get_if<NumberLit>(&prop->value.node)) {
                    type = ValueType::Number;
                } else if (const auto* unary =
                               std::get_if<UnaryExpr>(&prop->value.node)) {
                    // A signed number literal (-1 / +2).
                    if ((unary->op != '-' && unary->op != '+') ||
                        !std::get_if<NumberLit>(&unary->operand->node))
                        throw Error(prop->loc,
                                    "ListElement role '" + prop->name +
                                        "' must be a literal (number, string or bool)");
                    type = ValueType::Number;
                } else if (const auto* lit =
                               std::get_if<StringLit>(&prop->value.node)) {
                    type = isColorLiteral(lit->value) ? ValueType::Color
                                                      : ValueType::String;
                } else if (std::get_if<BoolLit>(&prop->value.node)) {
                    type = ValueType::Bool;
                } else {
                    throw Error(prop->loc,
                                "ListElement role '" + prop->name +
                                    "' must be a literal (number, string or bool)");
                }
                obj.roles.emplace(prop->name, type); // the first element wins
            }
        }
    }

    void registerId(ObjectInfo& obj, const PropertyStmt& stmt) {
        if (obj.delegateScope)
            throw Error(stmt.loc, "'id' is not allowed inside a Repeater delegate");
        if (obj.type->kind == TypeKind::ModelElement)
            throw Error(stmt.loc, "'ListElement' cannot have an id");
        if (obj.type->kind == TypeKind::Connections ||
            obj.type->kind == TypeKind::Binding)
            throw Error(stmt.loc, "'" + obj.type->name + "' cannot have an id");
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

    // ------------------------- script functions ---------------------------

    /// The value type of a function parameter/return type name.
    static ValueType functionValueType(const std::string& typeName,
                                       const SourceLoc& loc) {
        if (typeName == "number")
            return ValueType::Number;
        if (typeName == "string")
            return ValueType::String;
        if (typeName == "bool")
            return ValueType::Bool;
        if (typeName == "color")
            return ValueType::Color;
        throw Error(loc, "unsupported function type '" + typeName +
                             "' (supported: number, string, bool, color)");
    }

    /// Pass 1: registers a `function name(arg: type, ...) -> type` signature
    /// so calls resolve regardless of the declaration order (bodies are
    /// checked lazily by checkFunctionBody).
    void registerFunction(ObjectInfo& obj, const FunctionDecl& decl) {
        if (obj.type->kind == TypeKind::Timer || obj.type->kind == TypeKind::Model ||
            obj.type->kind == TypeKind::ModelElement ||
            obj.type->kind == TypeKind::Connections ||
            obj.type->kind == TypeKind::Binding ||
            obj.type->kind == TypeKind::Component)
            throw Error(decl.loc, "'" + obj.type->name + "' does not support functions");
        if (obj.delegateScope)
            throw Error(decl.loc, "functions are not supported inside a "
                                  "Repeater delegate");
        if (obj.id.empty())
            throw Error(decl.loc, "a function requires the object to have an id "
                                  "(the call receiver)");
        if (decl.name.empty() || decl.name[0] < 'a' || decl.name[0] > 'z')
            throw Error(decl.loc, "function name '" + decl.name +
                                      "' must start with a lowercase letter");
        if (obj.type->findProperty(decl.name) || obj.type->findMethod(decl.name))
            throw Error(decl.loc, "type '" + obj.type->name +
                                      "' already has a member '" + decl.name + "'");
        if (findCustomFunction(obj, decl.name))
            throw Error(decl.loc, "function '" + decl.name + "' is declared twice");

        FunctionInfo info;
        info.loc = decl.loc;
        info.decl = &decl;
        info.owner = &obj;
        std::set<std::string> paramNames;
        for (const HandlerParam& param : decl.params) {
            if (!paramNames.insert(param.name).second)
                throw Error(param.loc, "duplicate function argument '" + param.name + "'");
            info.paramTypes.push_back(functionValueType(param.typeName, param.loc));
        }
        info.isVoid = decl.returnTypeName.empty();
        if (!info.isVoid)
            info.returnType = functionValueType(decl.returnTypeName, decl.returnTypeLoc);
        if (info.isVoid && decl.returnValue)
            throw Error(decl.returnLoc,
                        "function '" + decl.name + "' has no return type but returns "
                            "a value (add '-> <type>' or drop the 'return')");
        if (!info.isVoid && !decl.returnValue)
            throw Error(decl.loc, "function '" + decl.name + "' must end with "
                                      "'return <expression>' (or drop '-> " +
                                      decl.returnTypeName + "')");
        obj.functions.push_back(std::move(info));
    }

    /// Validates a function body (once, lazily): the commands follow the
    /// handler-command rules; the trailing `return <expr>` is type-checked
    /// against the declared return type and its property deps are recorded so
    /// call sites in bindings can re-export them.
    void checkFunctionBody(ObjectInfo& owner, FunctionInfo& fn) {
        if (fn.bodyChecked)
            return;
        if (fn.bodyChecking)
            throw Error(fn.loc, "recursive call of function '" + fn.decl->name +
                                    "' (recursion is not supported)");
        fn.bodyChecking = true;
        // The body's scope is the owner object plus the parameters; any
        // handler locals of an enclosing check are saved and restored.
        const auto savedLocals = std::move(m_expressionLocals);
        const auto savedObjects = std::move(m_localObjects);
        m_expressionLocals.clear();
        m_localObjects.clear();
        for (std::size_t i = 0; i < fn.decl->params.size(); ++i)
            m_expressionLocals[fn.decl->params[i].name] = fn.paramTypes[i];
        for (const Expr& command : fn.decl->commands)
            checkBodyCommand(owner, fn.componentLocals, command);
        if (fn.decl->returnValue) {
            std::vector<std::string> deps;
            const ValueType actual = checkExpr(*fn.decl->returnValue, owner, deps);
            checkFunctionValue(fn, "the return value", fn.returnType, actual,
                               *fn.decl->returnValue);
            fn.returnDeps = std::move(deps);
        }
        m_expressionLocals = std::move(savedLocals);
        m_localObjects = std::move(savedObjects);
        fn.bodyChecking = false;
        fn.bodyChecked = true;
    }

    /// Type-checks one function call argument or the return expression.
    void checkFunctionValue(const FunctionInfo& fn, const std::string& what,
                            ValueType expected, ValueType actual,
                            const Expr& value) const {
        if (expected == actual || actual == ValueType::Any ||
            expected == ValueType::Any)
            return;
        // A String is accepted where a Color is expected; bare literals are
        // validated against the #RRGGBB form.
        if (expected == ValueType::Color && actual == ValueType::String) {
            if (const auto* lit = std::get_if<StringLit>(&value.node))
                if (!isColorLiteral(lit->value))
                    throw Error(value.loc, "invalid color literal '" + lit->value +
                                               "' (expected #RRGGBB)");
            return;
        }
        throw Error(value.loc, what + " of function '" + fn.decl->name + "' expects " +
                                   valueTypeName(expected) + ", got " +
                                   valueTypeName(actual));
    }

    /// Shared validation of a script-function call: the body is checked (once)
    /// and the argument count/types are validated. When `deps` is non-null
    /// (expression context) the argument deps and the callee's
    /// return-expression deps are appended to it.
    void checkFunctionCall(FunctionInfo& fn, const MethodCallExpr& call,
                           ObjectInfo& scope, const SourceLoc& loc,
                           std::vector<std::string>* deps) {
        checkFunctionBody(*fn.owner, fn);
        if (call.args.size() != fn.paramTypes.size())
            throw Error(loc, "function '" + fn.decl->name + "' expects " +
                                 std::to_string(fn.paramTypes.size()) +
                                 " argument(s)");
        for (std::size_t i = 0; i < call.args.size(); ++i) {
            std::vector<std::string> argDeps;
            const ValueType actual = checkExpr(call.args[i], scope, argDeps);
            checkFunctionValue(fn, "argument " + std::to_string(i + 1),
                               fn.paramTypes[i], actual, call.args[i]);
            if (deps)
                for (const std::string& dep : argDeps)
                    addDep(*deps, dep);
        }
        if (deps)
            for (const std::string& dep : fn.returnDeps)
                addDep(*deps, dep);
    }

    // ---------------------- pass 2: members, anchors ----------------------

    void processObject(ObjectInfo& obj) {
        // A ListModel's members were fully processed in pass 1 (roles); the
        // ListElement children never get a member pass of their own.
        if (obj.type->kind == TypeKind::Model) {
            for (const MemberNode& member : obj.node->members) {
                if (std::get_if<ObjectPtr>(&member))
                    continue;
                const auto* prop = std::get_if<PropertyStmt>(&member);
                if (prop && prop->name == "id")
                    continue;
                throw Error(obj.node->loc,
                            "'ListModel' members must be ListElement objects");
            }
            return;
        }
        // Connections/Binding own no runtime object: their members resolve
        // against the target and feed the generic handler/binding tables.
        if (obj.type->kind == TypeKind::Connections) {
            processConnections(obj);
            return;
        }
        if (obj.type->kind == TypeKind::Binding) {
            processBinding(obj);
            return;
        }
        // A Component declares a deferred subtree: only `id` plus exactly one
        // child object (the template root) are allowed as members.
        if (obj.type->kind == TypeKind::Component) {
            processComponent(obj);
            return;
        }
        const bool isRepeater = obj.type == builtinType("Repeater");
        for (const MemberNode& member : obj.node->members) {
            if (const auto* prop = std::get_if<PropertyStmt>(&member)) {
                if (prop->name != "id" && !(isRepeater && prop->name == "model"))
                    processProperty(obj, *prop);
            } else if (const auto* decl = std::get_if<PropertyDecl>(&member)) {
                if (obj.delegateScope)
                    throw Error(decl->loc, "custom properties are not supported "
                                           "inside a Repeater delegate");
                processPropertyDecl(obj, *decl);
            } else if (const auto* anchor = std::get_if<AnchorStmt>(&member)) {
                if (obj.type->kind != TypeKind::Item)
                    throw Error(anchor->loc, "anchors are only valid on items, not on '" +
                                                 obj.type->name + "'");
                if (obj.delegateScope)
                    throw Error(anchor->loc, "anchors are not supported inside a "
                                             "Repeater delegate yet");
                processAnchor(obj, *anchor);
            } else if (const auto* behavior = std::get_if<BehaviorStmt>(&member)) {
                if (obj.type->kind != TypeKind::Item)
                    throw Error(behavior->loc,
                                "behaviors are only valid on items, not on '" +
                                    obj.type->name + "'");
                if (obj.delegateScope)
                    throw Error(behavior->loc, "behaviors are not supported inside a "
                                               "Repeater delegate yet");
                processBehavior(obj, *behavior);
            } else if (const auto* handler = std::get_if<HandlerStmt>(&member)) {
                processHandler(obj, obj, *handler);
            } else if (const auto* fn = std::get_if<FunctionDecl>(&member)) {
                // The signature was registered in pass 1; make sure the body
                // is checked even when the function is never called.
                checkFunctionBody(obj, *findCustomFunction(obj, fn->name));
            }
        }
        if (isRepeater)
            validateRepeater(obj);
        checkOverconstrained(obj);
        for (ObjectInfo* child : obj.children)
            processObject(*child);
    }

    /// Component: requires an id (the createObject() receiver) and exactly
    /// one child object — the deferred subtree template.
    void processComponent(ObjectInfo& obj) {
        if (obj.id.empty())
            throw Error(obj.node->loc,
                        "a Component needs an id (used as the createObject() "
                        "receiver)");
        const Object* templateNode = nullptr;
        for (const MemberNode& member : obj.node->members) {
            if (const auto* prop = std::get_if<PropertyStmt>(&member)) {
                if (prop->name == "id")
                    continue;
                throw Error(prop->loc, "a Component has no properties (only a "
                                       "single child object — the template)");
            }
            if (const auto* child = std::get_if<ObjectPtr>(&member)) {
                if (templateNode)
                    throw Error((*child)->loc, "a Component holds exactly one "
                                               "child object (the template)");
                templateNode = child->get();
                continue;
            }
            throw Error(obj.node->loc,
                        "a Component cannot have handlers, anchors, behaviors "
                        "or property declarations of its own");
        }
        if (!templateNode)
            throw Error(obj.node->loc, "a Component needs exactly one child "
                                       "object (the template)");
        if (obj.children.empty())
            throw Error(obj.node->loc, "internal error: Component template was "
                                       "not registered");
        obj.componentRoot = obj.children.front();
        processObject(*obj.componentRoot);
    }

    /// Repeater: resolves the `model` property (a ListModel id or a delegate
    /// count) and requires exactly one delegate child object.
    void validateRepeater(ObjectInfo& obj) {
        bool hasModel = false;
        for (const MemberNode& member : obj.node->members) {
            const auto* prop = std::get_if<PropertyStmt>(&member);
            if (!prop || prop->name != "model")
                continue;
            hasModel = true;
            resolveRepeaterModel(obj, *prop);
        }
        if (!hasModel)
            throw Error(obj.node->loc, "'Repeater' requires a 'model' property");
        if (obj.children.size() != 1)
            throw Error(obj.node->loc,
                        "'Repeater' requires exactly one delegate child object");
    }

    void resolveRepeaterModel(ObjectInfo& obj, const PropertyStmt& stmt) {
        if (const auto* ident = std::get_if<IdentExpr>(&stmt.value.node)) {
            const auto it = m_result.byId.find(ident->name);
            if (it == m_result.byId.end())
                throw Error(stmt.loc, "unknown model '" + ident->name +
                                          "' (expected a ListModel id)");
            if (it->second->type->kind != TypeKind::Model)
                throw Error(stmt.loc, "the Repeater model '" + ident->name + "' is a '" +
                                          it->second->type->name +
                                          "', expected a ListModel");
            obj.modelObject = it->second;
            return;
        }
        if (const auto* lit = std::get_if<NumberLit>(&stmt.value.node)) {
            if (lit->text.find_first_not_of("0123456789") != std::string::npos)
                throw Error(stmt.loc,
                            "a numeric Repeater model must be a non-negative integer");
            obj.modelCount = std::stoi(lit->text);
            return;
        }
        throw Error(stmt.loc, "the Repeater 'model' must be a ListModel id or a number");
    }

    /// `property <type> <name>[: <expr>]`: validates the declaration and
    /// registers the dynamic property. The optional initializer goes through
    /// the ordinary binding machinery (static write or dependency-tracked
    /// binding); without an initializer codegen writes the type default.
    void processPropertyDecl(ObjectInfo& obj, const PropertyDecl& decl) {
        ValueType type;
        if (decl.typeName == "number")
            type = ValueType::Number;
        else if (decl.typeName == "string")
            type = ValueType::String;
        else if (decl.typeName == "bool")
            type = ValueType::Bool;
        else if (decl.typeName == "color")
            type = ValueType::Color;
        else
            throw Error(decl.loc, "unknown property type '" + decl.typeName +
                                      "' (supported: number, string, bool, color)");
        if (decl.name.empty() || decl.name[0] < 'a' || decl.name[0] > 'z')
            throw Error(decl.loc, "property name '" + decl.name +
                                      "' must start with a lowercase letter");
        if (obj.type->kind == TypeKind::Timer ||
            obj.type->kind == TypeKind::Model ||
            obj.type->kind == TypeKind::ModelElement)
            throw Error(decl.loc, "'" + obj.type->name +
                                      "' does not support custom properties");
        if (decl.name == "id")
            throw Error(decl.loc, "a custom property cannot be named 'id'");
        if (obj.type->findProperty(decl.name))
            throw Error(decl.loc, "type '" + obj.type->name +
                                      "' already has a property '" + decl.name + "'");
        if (obj.propLocs.count(decl.name))
            throw Error(decl.loc, "property '" + decl.name + "' is already assigned");
        if (findCustomProp(obj, decl.name))
            throw Error(decl.loc, "property '" + decl.name + "' is declared twice");
        if (findCustomFunction(obj, decl.name))
            throw Error(decl.loc, "a function named '" + decl.name +
                                      "' is already declared");

        CustomPropInfo info;
        info.loc = decl.loc;
        info.decl = &decl;
        info.type = type;
        obj.customProps.push_back(info);

        if (!decl.hasValue)
            return;
        std::vector<std::string> deps;
        const ValueType actual = checkExpr(decl.init.value, obj, deps);
        checkAssignable(decl.loc, decl.name, type, actual, decl.init.value);
        recordBinding(obj, decl.init, deps);
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
        if (obj.type->kind == TypeKind::Timer && !deps.empty())
            throw Error(stmt.loc, "Timer properties do not support bindings");
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
        // Any: an opaque/runtime value — converted at runtime (anyConvert).
        if (actual == ValueType::Any || expected == ValueType::Any)
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
            if (condType != ValueType::Bool && condType != ValueType::Any)
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
        if (const auto* call = std::get_if<MethodCallExpr>(&expr.node))
            return checkScriptCall(*call, expr.loc, scope, deps);
        if (std::get_if<AssignExpr>(&expr.node))
            throw Error(expr.loc, "assignments are only allowed in signal/property handlers");
        throw Error(expr.loc, "unsupported expression");
    }

    ValueType checkIdent(const IdentExpr& ident, const SourceLoc& loc, ObjectInfo& scope,
                         std::vector<std::string>& deps) {
        if (const auto local = m_expressionLocals.find(ident.name);
            local != m_expressionLocals.end())
            return local->second;
        if (scope.delegateScope) {
            // Inside a Repeater delegate: the row index and model roles.
            if (ident.name == "index")
                return ValueType::Number;
            if (ident.name == "model")
                throw Error(loc, "'model' must be used as model.<role>");
        }
        if (ident.name == "parent")
            throw Error(loc, "'parent' must be used as parent.<property>");
        if (const PropertyInfo* prop = scope.type->findProperty(ident.name)) {
            addDep(deps, scope.internalName + "." + ident.name);
            return prop->type;
        }
        if (const CustomPropInfo* custom = findCustomProp(scope, ident.name)) {
            addDep(deps, scope.internalName + "." + ident.name);
            return custom->type;
        }
        if (m_result.byId.count(ident.name))
            throw Error(loc, "unexpected object id '" + ident.name +
                                 "' in an expression (use " + ident.name + ".<property>)");
        throw Error(loc, "unknown identifier '" + ident.name + "'");
    }

    /// Resolves a `<singleton>.<property>` read (e.g. Firmir.Gui.App.screenWidth).
    /// The dependency is registered under the canonical singleton name.
    ValueType checkSingletonMember(const std::string& writtenName,
                                   const std::string& property,
                                   const TypeInfo* singleton, const SourceLoc& loc,
                                   std::vector<std::string>& deps) {
        const PropertyInfo* prop = singleton->findProperty(property);
        if (!prop)
            throw Error(loc, "unknown property '" + property + "' of singleton '" +
                                 writtenName + "'");
        noteSingletonUse(writtenName);
        addDep(deps, canonicalSingletonName(writtenName) + "." + property);
        return prop->type;
    }

    /// Records which singleton the file uses so codegen can emit the needed
    /// includes / instance setup.
    void noteSingletonUse(const std::string& writtenName) {
        const std::string canonical = canonicalSingletonName(writtenName);
        if (canonical == "Firmir.Gui.App")
            m_result.usesAppSingleton = true;
        else if (canonical == "Firmir.Gui.Theme")
            m_result.usesThemeSingleton = true;
    }

    ValueType checkMember(const MemberExpr& member, const SourceLoc& loc,
                          ObjectInfo& scope, std::vector<std::string>& deps) {
        const auto* base = std::get_if<IdentExpr>(&member.object->node);
        if (!base) {
            // A dotted singleton reference like Firmir.Gui.App.screenWidth.
            const std::string dotted = dottedName(*member.object);
            if (const TypeInfo* singleton = builtinSingleton(dotted))
                return checkSingletonMember(dotted, member.name, singleton, loc, deps);
            const ValueType objectType = checkExpr(*member.object, scope, deps);
            if (objectType == ValueType::Rect &&
                (member.name == "x" || member.name == "y" ||
                 member.name == "width" || member.name == "height"))
                return ValueType::Number;
            throw Error(loc, "member access is only supported on ids, 'parent', "
                             "singletons and Rect.x/y/width/height");
        }
        if (base->name == "model" && scope.delegateScope) {
            // Role access inside a Repeater delegate: model.<role>.
            const ObjectInfo* repeater = scope.delegateScope;
            if (!repeater->modelObject)
                throw Error(loc, "the Repeater model is a plain number; 'model." +
                                 member.name + "' is unavailable");
            const auto role = repeater->modelObject->roles.find(member.name);
            if (role == repeater->modelObject->roles.end()) {
                // A model without static rows is filled at runtime (e.g.
                // ListModel::append from C++): its roles cannot be checked.
                if (repeater->modelObject->roles.empty()) {
                    warn(loc, "cannot verify role '" + member.name +
                                  "' of runtime-filled ListModel '" +
                                  repeater->modelObject->id + "'");
                    return ValueType::Any;
                }
                throw Error(loc, "ListModel '" + repeater->modelObject->id +
                                     "' has no role '" + member.name + "'");
            }
            return role->second; // a static model: no dependency to track
        }
        const ObjectInfo* target = nullptr;
        if (base->name == "parent") {
            target = scope.parent;
            if (!target)
                throw Error(loc, "the root object has no 'parent'");
        } else {
            const auto it = m_result.byId.find(base->name);
            if (it == m_result.byId.end()) {
                // The short singleton form: App.screenWidth.
                if (const TypeInfo* singleton = builtinSingleton(base->name))
                    return checkSingletonMember(base->name, member.name, singleton, loc,
                                                deps);
                if (scope.type->findProperty(base->name))
                    throw Error(loc, "'" + base->name +
                                         "' is a property, not an object id (use plain '" +
                                         base->name + "')");
                throw Error(loc, "unknown identifier '" + base->name + "'");
            }
            target = it->second;
        }
        const PropertyInfo* prop = target->type->findProperty(member.name);
        if (!prop) {
            if (const CustomPropInfo* custom = findCustomProp(*target, member.name)) {
                addDep(deps, target->internalName + "." + member.name);
                return custom->type;
            }
            if (target->type->opaque) {
                // Opaque types expose their properties through the dynamic
                // Object property bag (getProperty/anyConvert at runtime).
                warn(loc, "cannot verify property '" + member.name +
                              "' of opaque type '" + target->type->name +
                              "' (no meta information)");
                addDep(deps, target->internalName + "." + member.name);
                return ValueType::Any;
            }
            throw Error(loc, "unknown property '" + member.name + "' of type '" +
                                 target->type->name + "'");
        }
        addDep(deps, target->internalName + "." + member.name);
        return prop->type;
    }

    ValueType checkCall(const CallExpr& call, const SourceLoc& loc, ObjectInfo& scope,
                        std::vector<std::string>& deps) {
        BuiltinFunction function;
        if (!lookupBuiltinFunction(call.callee, function))
            throw Error(loc, "unknown function '" + call.callee +
                                 "' (supported: min, max, lighter, darker, trId)");
        if (function == BuiltinFunction::TrId) {
            // trId("some-id") -> translated text in the active language. The
            // binding depends on the translator's language property so all
            // trId() texts switch immediately when the language changes.
            if (call.args.size() != 1)
                throw Error(loc, "'trId' expects exactly 1 argument");
            const ValueType argType = checkExpr(call.args[0], scope, deps);
            if (argType != ValueType::String && argType != ValueType::Any)
                throw Error(call.args[0].loc,
                            "'trId' expects a String id argument, got " +
                                std::string(valueTypeName(argType)));
            m_result.usesTranslatorSingleton = true;
            addDep(deps, "Firmir.Core.Translator.language");
            return ValueType::String;
        }
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

    /// An `id.function(...)` / `parent.function(...)` call inside a value
    /// expression: only script-declared functions are callable here (native
    /// methods stay handler-only). The call site depends on the argument
    /// expressions plus the callee's return-expression deps.
    ValueType checkScriptCall(const MethodCallExpr& call, const SourceLoc& loc,
                              ObjectInfo& scope, std::vector<std::string>& deps) {
        const auto* receiverIdent = std::get_if<IdentExpr>(&call.object->node);
        if (!receiverIdent)
            throw Error(loc, "only script-declared functions can be called in "
                             "expressions (id.function(...))");
        ObjectInfo* target = nullptr;
        if (receiverIdent->name == "parent") {
            if (!scope.parent)
                throw Error(loc, "the root object has no 'parent'");
            target = scope.parent;
        } else {
            const auto it = m_result.byId.find(receiverIdent->name);
            if (it == m_result.byId.end())
                throw Error(loc, "unknown object id '" + receiverIdent->name +
                                     "' in the function call");
            target = it->second;
        }
        if (target->componentScope != scope.componentScope)
            throw Error(loc, "'" + receiverIdent->name +
                                 "' is local to a Component and cannot be used here");
        FunctionInfo* fn = findCustomFunction(*target, call.method);
        if (!fn)
            throw Error(loc, "unknown function '" + call.method + "' of object '" +
                                 receiverIdent->name + "'");
        if (fn->isVoid)
            throw Error(loc, "function '" + call.method +
                                 "' does not return a value and cannot be used in "
                                 "an expression");
        checkFunctionCall(*fn, call, scope, loc, &deps);
        return fn->returnType;
    }

    ValueType checkBinary(const BinaryExpr& binary, const SourceLoc& loc,
                          ObjectInfo& scope, std::vector<std::string>& deps) {
        const ValueType lhs = checkExpr(*binary.lhs, scope, deps);
        const ValueType rhs = checkExpr(*binary.rhs, scope, deps);
        const std::string& op = binary.op;
        if (op == "&&" || op == "||") {
            if ((lhs != ValueType::Bool && lhs != ValueType::Any) ||
                (rhs != ValueType::Bool && rhs != ValueType::Any))
                throw Error(loc, "operator '" + op + "' requires Bool operands, got " +
                                     std::string(valueTypeName(lhs)) + " and " +
                                     valueTypeName(rhs));
            return ValueType::Bool;
        }
        if (op == "==" || op == "!=") {
            if (lhs != rhs && lhs != ValueType::Any && rhs != ValueType::Any)
                throw Error(loc, "cannot compare " + std::string(valueTypeName(lhs)) +
                                     " and " + valueTypeName(rhs) + " with '" + op + "'");
            return ValueType::Bool;
        }
        const bool arithmetic = op == "+" || op == "-" || op == "*" || op == "/" || op == "%";
        // String concatenation: at least one String operand, the other side
        // may be a Number (converted with String::number() in codegen).
        if (op == "+" && (lhs == ValueType::String || rhs == ValueType::String)) {
            const bool ok = (lhs != ValueType::Color && lhs != ValueType::Bool &&
                             lhs != ValueType::Rect) &&
                            (rhs != ValueType::Color && rhs != ValueType::Bool &&
                             rhs != ValueType::Rect);
            if (!ok)
                throw Error(loc, "operator '+' cannot concatenate " +
                                     std::string(valueTypeName(lhs)) + " and " +
                                     valueTypeName(rhs) +
                                     " (only String and Number operands)");
            return ValueType::String;
        }
        if ((lhs != ValueType::Number && lhs != ValueType::Any) ||
            (rhs != ValueType::Number && rhs != ValueType::Any))
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

    /// Validates a `<target> = <value>` handler command: the target resolves
    /// to a writable property of an id / parent / the handler's own object.
    void checkAssignCommand(ObjectInfo& obj, const AssignExpr& assign) {
        const Expr& targetExpr = *assign.target;
        const ObjectInfo* targetObj = nullptr;
        const TypeInfo* singletonType = nullptr; // set for `Theme.x = ...`
        std::string propName;
        if (const auto* ident = std::get_if<IdentExpr>(&targetExpr.node)) {
            if (ident->name == "parent")
                throw Error(targetExpr.loc,
                            "invalid assignment target 'parent' (use parent.<property>)");
            targetObj = &obj;
            propName = ident->name;
        } else if (const auto* member = std::get_if<MemberExpr>(&targetExpr.node)) {
            const auto* base = std::get_if<IdentExpr>(&member->object->node);
            if (!base) {
                if (const TypeInfo* singleton =
                        builtinSingleton(dottedName(*member->object))) {
                    singletonType = singleton;
                    noteSingletonUse(dottedName(*member->object));
                    propName = member->name;
                } else {
                    throw Error(targetExpr.loc,
                                "the assignment target must be a property "
                                "name or <id>.<property>");
                }
            } else if (base->name == "parent") {
                if (!obj.parent)
                    throw Error(targetExpr.loc, "the root object has no 'parent'");
                targetObj = obj.parent;
                propName = member->name;
            } else if (const TypeInfo* singleton = builtinSingleton(base->name)) {
                singletonType = singleton;
                noteSingletonUse(base->name);
                propName = member->name;
            } else {
                const auto it = m_result.byId.find(base->name);
                if (it == m_result.byId.end()) {
                    // A `const obj = comp.createObject(...)` local: the write
                    // goes to the created object's dynamic property bag.
                    const auto local = m_localObjects.find(base->name);
                    if (local == m_localObjects.end())
                        throw Error(targetExpr.loc,
                                    "unknown identifier '" + base->name + "'");
                    const ObjectInfo* localRoot = local->second;
                    const PropertyInfo* localProp =
                        localRoot->type->findProperty(member->name);
                    const bool known = localProp ||
                        findCustomProp(*localRoot, member->name);
                    if (!known && !localRoot->type->opaque)
                        throw Error(targetExpr.loc,
                                    "unknown property '" + member->name +
                                        "' of type '" + localRoot->type->name + "'");
                    if (localProp && localProp->readOnly)
                        throw Error(targetExpr.loc,
                                    "property '" + member->name + "' of type '" +
                                        localRoot->type->name + "' is read-only");
                    std::vector<std::string> ignoredDeps;
                    checkExpr(*assign.value, obj, ignoredDeps);
                    return;
                }
                targetObj = it->second;
                propName = member->name;
            }
        } else {
            throw Error(targetExpr.loc, "the assignment target must be a property "
                                        "name or <id>.<property>");
        }
        if (singletonType) {
            // Writable singleton properties (e.g. Theme.highlightColor) may
            // be assigned in handlers; read-only ones are rejected.
            const PropertyInfo* prop = singletonType->findProperty(propName);
            if (!prop)
                throw Error(targetExpr.loc,
                            "unknown property '" + propName + "' of singleton");
            if (prop->readOnly)
                throw Error(targetExpr.loc, "singleton property '" + propName +
                                                "' is read-only");
            std::vector<std::string> ignoredDeps;
            const ValueType actual = checkExpr(*assign.value, obj, ignoredDeps);
            checkAssignable(targetExpr.loc, propName, prop->type, actual,
                            *assign.value);
            return;
        }
        if (targetObj->type->kind == TypeKind::Timer)
            throw Error(targetExpr.loc, "Timer properties cannot be assigned in "
                                        "handlers (use start/stop methods)");
        const PropertyInfo* prop = targetObj->type->findProperty(propName);
        const CustomPropInfo* custom =
            prop ? nullptr : findCustomProp(*targetObj, propName);
        if (!prop && !custom) {
            if (targetObj->type->opaque)
                warn(targetExpr.loc, "cannot verify property '" + propName +
                                         "' of opaque type '" + targetObj->type->name +
                                         "' (no meta information)");
            else
                throw Error(targetExpr.loc, "unknown property '" + propName +
                                                "' of type '" + targetObj->type->name +
                                                "'");
        }
        if (prop && prop->readOnly)
            throw Error(targetExpr.loc, "property '" + propName + "' of type '" +
                                            targetObj->type->name + "' is read-only");
        std::vector<std::string> ignoredDeps;
        const ValueType actual = checkExpr(*assign.value, obj, ignoredDeps);
        if (prop)
            checkAssignable(targetExpr.loc, propName, prop->type, actual, *assign.value);
        else if (custom)
            checkAssignable(targetExpr.loc, propName, custom->type, actual,
                            *assign.value);
    }

    // --------------------- connections / binding (QML) ---------------------

    /// `target: <id>` shared by Connections and Binding.
    const ObjectInfo& resolveTargetRef(const PropertyStmt& stmt,
                                       const char* owner) const {
        const auto* ident = std::get_if<IdentExpr>(&stmt.value.node);
        if (!ident)
            throw Error(stmt.loc, "'" + std::string(owner) +
                                      ".target' must be an object id");
        const auto it = m_result.byId.find(ident->name);
        if (it == m_result.byId.end())
            throw Error(stmt.loc, "unknown id '" + ident->name + "' in '" +
                                      owner + ".target'");
        return *it->second;
    }

    /// Connections { target: <id>; onXxx: ... } — the handlers are validated
    /// against the target's type and recorded with the target as the signal
    /// owner; the Connections object itself is the command scope.
    void processConnections(ObjectInfo& obj) {
        const ObjectInfo* target = nullptr;
        for (const MemberNode& member : obj.node->members) {
            const auto* prop = std::get_if<PropertyStmt>(&member);
            if (!prop)
                continue;
            if (prop->name != "target")
                throw Error(prop->loc, "unknown property '" + prop->name +
                                           "' of type 'Connections'");
            target = &resolveTargetRef(*prop, "Connections");
        }
        if (!target)
            throw Error(obj.node->loc, "'Connections' requires a 'target' property");
        for (const MemberNode& member : obj.node->members) {
            if (std::get_if<PropertyStmt>(&member))
                continue; // 'target', resolved above
            const auto* handler = std::get_if<HandlerStmt>(&member);
            if (!handler)
                throw Error(obj.node->loc,
                            "'Connections' only supports 'target' and signal handlers");
            processHandler(obj, const_cast<ObjectInfo&>(*target), *handler);
        }
    }

    /// Binding { target: <id>; property: "name"; value: <expr> } — records an
    /// ordinary binding whose object is the target; codegen needs nothing
    /// special (the binding tables are object-agnostic).
    void processBinding(ObjectInfo& obj) {
        const ObjectInfo* target = nullptr;
        const PropertyStmt* propertyStmt = nullptr;
        const PropertyStmt* valueStmt = nullptr;
        for (const MemberNode& member : obj.node->members) {
            const auto* prop = std::get_if<PropertyStmt>(&member);
            if (!prop)
                throw Error(obj.node->loc,
                            "'Binding' only supports 'target', 'property' and 'value'");
            if (prop->name == "target")
                target = &resolveTargetRef(*prop, "Binding");
            else if (prop->name == "property")
                propertyStmt = prop;
            else if (prop->name == "value")
                valueStmt = prop;
            else
                throw Error(prop->loc, "unknown property '" + prop->name +
                                           "' of type 'Binding'");
        }
        if (!target)
            throw Error(obj.node->loc, "'Binding' requires a 'target' property");
        if (!propertyStmt)
            throw Error(obj.node->loc, "'Binding' requires a 'property' property");
        if (!valueStmt)
            throw Error(obj.node->loc, "'Binding' requires a 'value' property");
        const auto* nameLit = std::get_if<StringLit>(&propertyStmt->value.node);
        if (!nameLit || nameLit->value.empty())
            throw Error(propertyStmt->loc,
                        "'Binding.property' must be a non-empty string");
        const std::string& propName = nameLit->value;
        const PropertyInfo* prop = target->type->findProperty(propName);
        const CustomPropInfo* custom = prop ? nullptr : findCustomProp(*target, propName);
        if (!prop && !custom)
            throw Error(propertyStmt->loc, "unknown property '" + propName +
                                               "' of type '" + target->type->name + "'");
        if (prop && prop->readOnly)
            throw Error(propertyStmt->loc, "property '" + propName + "' of type '" +
                                               target->type->name + "' is read-only");
        std::vector<std::string> deps;
        const ValueType actual = checkExpr(valueStmt->value, obj, deps);
        checkAssignable(valueStmt->loc, propName, prop ? prop->type : custom->type,
                        actual, valueStmt->value);
        BindingInfo binding;
        binding.loc = valueStmt->loc;
        binding.object = target;
        binding.property = propName;
        binding.stmt = valueStmt;
        binding.deps = deps;
        binding.isStatic = deps.empty();
        m_result.bindings.push_back(binding);
        const std::string from = target->internalName + "." + propName;
        for (const std::string& dep : deps)
            addEdge(from, dep, valueStmt->loc);
    }

    /// Finds a Component by id (nullptr when the id is unknown or not a
    /// Component).
    const ObjectInfo* findComponentById(const std::string& name) {
        const auto it = m_result.byId.find(name);
        if (it == m_result.byId.end() ||
            it->second->type->kind != TypeKind::Component)
            return nullptr;
        return it->second;
    }

    /// Validates the single parent argument of comp.createObject(parent):
    /// an object id or 'parent' (inside a Component template root it is the
    /// factory's runtime parent).
    void checkCreateObjectArgs(ObjectInfo& context, const MethodCallExpr& call,
                               const SourceLoc& loc) {
        if (call.args.size() != 1)
            throw Error(loc, "createObject() expects exactly 1 argument "
                             "(the parent object)");
        const auto* argIdent = std::get_if<IdentExpr>(&call.args[0].node);
        if (!argIdent)
            throw Error(call.args[0].loc,
                        "the createObject() parent must be an object id or "
                        "'parent'");
        if (argIdent->name == "parent") {
            if (!context.parent)
                throw Error(call.args[0].loc, "the root object has no 'parent'");
            return;
        }
        const auto it = m_result.byId.find(argIdent->name);
        if (it == m_result.byId.end())
            throw Error(call.args[0].loc,
                        "unknown object id '" + argIdent->name + "'");
        // Ids inside a Component template are per-instance factory locals:
        // only handlers of the same component may use them.
        if (it->second->componentScope &&
            it->second->componentScope != context.componentScope)
            throw Error(call.args[0].loc,
                        "'" + argIdent->name +
                            "' is local to a Component and cannot be used here");
        if (it->second->type->kind != TypeKind::Item &&
            it->second->type->kind != TypeKind::Window)
            throw Error(call.args[0].loc, "the createObject() parent must be an "
                                          "item (or a window), not '" +
                                              it->second->type->name + "'");
    }

    /// `const name = someComponent.createObject(parent)` — declares a handler
    /// local holding the freshly created object.
    void checkConstDeclCommand(
        ObjectInfo& context,
        std::vector<std::pair<std::string, const ObjectInfo*>>& localsOut,
        const ConstDeclExpr& decl, const SourceLoc& loc) {
        const auto* call = std::get_if<MethodCallExpr>(&decl.value->node);
        if (!call)
            throw Error(loc, "'const <name> = ...' in a handler requires a "
                             "Component createObject() call on the right side");
        const auto* receiverIdent = std::get_if<IdentExpr>(&call->object->node);
        const ObjectInfo* component =
            receiverIdent ? findComponentById(receiverIdent->name) : nullptr;
        if (!component)
            throw Error(loc, "the receiver of createObject() must be a "
                             "Component id");
        if (call->method != "createObject")
            throw Error(loc, "a Component has only the createObject() method");
        checkCreateObjectArgs(context, *call, loc);
        if (m_expressionLocals.count(decl.name) || m_localObjects.count(decl.name) ||
            m_result.byId.count(decl.name))
            throw Error(loc, "duplicate name '" + decl.name + "'");
        m_localObjects[decl.name] = component->componentRoot;
        localsOut.push_back({decl.name, component->componentRoot});
    }

    /// Validates one handler/function body command: a method call (native or
    /// script-declared), a property assignment or a `const` local declaration.
    void checkBodyCommand(
        ObjectInfo& context,
        std::vector<std::pair<std::string, const ObjectInfo*>>& localsOut,
        const Expr& command) {
        if (const auto* decl = std::get_if<ConstDeclExpr>(&command.node)) {
            checkConstDeclCommand(context, localsOut, *decl, command.loc);
            return;
        }
        if (const auto* assign = std::get_if<AssignExpr>(&command.node)) {
            checkAssignCommand(context, *assign);
            return;
        }
        const auto* call = std::get_if<MethodCallExpr>(&command.node);
        if (!call)
            throw Error(command.loc,
                        "a handler command must be a method call (id.method(...)), "
                        "a property assignment (id.prop = ...) or a constant "
                        "declaration (const x = someComponent.createObject(...))");
        const std::string receiver = dottedName(*call->object);
        const TypeInfo* receiverType = nullptr;
        ObjectInfo* receiverObject = nullptr; // nullptr for singletons
        bool componentLocal = false;
        if (const TypeInfo* singleton = builtinSingleton(receiver)) {
            receiverType = singleton;
            noteSingletonUse(receiver);
        } else if (receiver == "parent") {
            if (!context.parent)
                throw Error(command.loc, "the root object has no 'parent'");
            receiverType = context.parent->type;
            receiverObject = context.parent;
        } else if (const auto local = m_localObjects.find(receiver);
                   local != m_localObjects.end()) {
            // A `const obj = comp.createObject(...)` local of this handler.
            receiverType = local->second->type;
            componentLocal = true;
        } else if (const auto it = m_result.byId.find(receiver);
                   it != m_result.byId.end()) {
            receiverType = it->second->type;
            receiverObject = it->second;
        } else {
            throw Error(command.loc, "unknown method receiver '" + receiver + "'");
        }
        // A Component's createObject(): the argument is an object
        // reference, not a value expression — checked separately.
        if (receiverType->kind == TypeKind::Component &&
            call->method == "createObject") {
            checkCreateObjectArgs(context, *call, command.loc);
            return;
        }
        // A script-declared function of the receiver object (shadows nothing:
        // registerFunction rejects names clashing with native members).
        if (receiverObject && !componentLocal) {
            if (FunctionInfo* fn = findCustomFunction(*receiverObject, call->method)) {
                if (receiverObject->componentScope != context.componentScope)
                    throw Error(command.loc, "'" + receiver +
                                                 "' is local to a Component and "
                                                 "cannot be used here");
                checkFunctionCall(*fn, *call, context, command.loc, nullptr);
                return;
            }
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
            checkExpr(arg, context, ignoredDeps);
        }
    }

    void processHandler(ObjectInfo& context, ObjectInfo& obj, const HandlerStmt& stmt) {
        if (stmt.name.size() <= 2)
            throw Error(stmt.loc, "invalid handler name '" + stmt.name + "'");
        std::string target = stmt.name.substr(2);
        HandlerInfo info;
        info.loc = stmt.loc;
        info.object = &obj;
        info.context = &context;
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
            if (!obj.type->findProperty(target) && !obj.type->opaque &&
                !findCustomProp(obj, target))
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
        m_localObjects.clear();
        for (std::size_t i = 0; i < stmt.params.size(); ++i) {
            const HandlerParam& param = stmt.params[i];
            if (!m_expressionLocals.emplace(param.name, handlerParamType(param)).second)
                throw Error(param.loc, "duplicate handler argument '" + param.name + "'");
            if (propertyChanged)
                break;
        }
        for (const Expr& command : stmt.commands)
            checkBodyCommand(context, info.componentLocals, command);
        m_expressionLocals.clear();
        m_localObjects.clear();
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
    /// The current handler's `const obj = comp.createObject(...)` locals:
    /// name -> the created root ObjectInfo.
    std::map<std::string, const ObjectInfo*> m_localObjects;
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

