#pragma once

#include "ast.hpp"
#include "types.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace Firmir {
namespace Fml {

struct ObjectInfo;

/// One resolved property assignment/binding.
struct BindingInfo {
    SourceLoc loc;
    const ObjectInfo* object = nullptr;
    std::string property;              // x, width, color, ...
    const PropertyStmt* stmt = nullptr; // the original AST node (for codegen)
    std::vector<std::string> deps;     // resolved "obj.prop", deduped, in order
    bool isStatic = false;             // no dependencies -> plain assignment
};

/// One resolved anchor. Margins/offsets keep their expression deps in `deps`;
/// line anchors resolve the target to an object + line and expand `deps` to
/// the target's geometry properties.
struct AnchorInfo {
    SourceLoc loc;
    const ObjectInfo* object = nullptr;
    std::string key;                    // left, fill, topMargin, ...
    const AnchorStmt* stmt = nullptr;
    std::string targetText;             // as written: parent.left, header, undefined
    const ObjectInfo* target = nullptr; // nullptr for `undefined` and margins
    std::string targetLine;             // left/right/... (empty for fill/centerIn/margins)
    std::vector<std::string> deps;      // resolved target-side "obj.prop"
};

/// One validated `behavior on <property>` statement. The animation tree stays
/// in the AST; codegen walks it (validation already happened here).
struct BehaviorInfo {
    SourceLoc loc;
    const ObjectInfo* object = nullptr;
    std::string property;
    const Object* animation = nullptr; // animation tree (AST), never null
};

enum class HandlerKind { Signal, PropertyChanged };

struct HandlerInfo {
    SourceLoc loc;
    const ObjectInfo* object = nullptr;
    HandlerKind kind = HandlerKind::Signal;
    std::string name; // signal name or property name
    const HandlerStmt* stmt = nullptr;
};

/// One script-declared (`property <type> <name>[: <expr>]`) dynamic property.
/// Lives in the runtime dynamic property bag; every write emits
/// propertyChanged, so bindings/handlers work like for native properties.
struct CustomPropInfo {
    SourceLoc loc;
    const PropertyDecl* decl = nullptr; // the AST node (for codegen)
    ValueType type = ValueType::Number;
};

struct ObjectInfo {
    const Object* node = nullptr;
    std::string resolvedTypeName; // after applying file-local using aliases
    const TypeInfo* type = nullptr;
    std::string id;           // empty when anonymous
    std::string internalName; // id or _obj_<N> (declaration order)
    ObjectInfo* parent = nullptr;
    std::vector<ObjectInfo*> children;
    int declIndex = 0;
    std::map<std::string, SourceLoc> propLocs; // for duplicate/overconstraint checks
    std::set<std::string> anchorKeys;          // filled while processing members
    std::vector<CustomPropInfo> customProps;   // `property` declarations, in order
};

/// Finds a script-declared property of the object (nullptr when none).
inline const CustomPropInfo* findCustomProp(const ObjectInfo& obj,
                                            const std::string& name) {
    for (const CustomPropInfo& prop : obj.customProps)
        if (prop.decl->name == name)
            return &prop;
    return nullptr;
}

struct SemaWarning {
    SourceLoc loc;
    std::string message;
};

/// An edge of the property dependency graph: `from` is recomputed from `to`.
struct DependencyEdge {
    std::string from; // "obj.prop"
    std::string to;   // "obj.prop"
};

struct SemaResult {
    std::vector<std::unique_ptr<ObjectInfo>> objectStorage; // stable pointers
    std::vector<std::unique_ptr<TypeInfo>> typeStorage;     // opaque types
    std::vector<ObjectInfo*> objects;                       // pre-order
    std::map<std::string, ObjectInfo*> byId;
    std::vector<BindingInfo> bindings;
    std::vector<AnchorInfo> anchors;
    std::vector<BehaviorInfo> behaviors;
    std::vector<HandlerInfo> handlers;
    std::vector<DependencyEdge> edges; // bindings + anchors, in file order
    std::vector<SemaWarning> warnings;
    /// True when the file references the Firmir.Gui.App singleton (handler
    /// method calls or screenWidth/screenHeight reads). A Window-rooted
    /// generated function then creates and registers the App instance itself.
    bool usesAppSingleton = false;
};

/// Runs semantic analysis on a parsed file: resolves the #include directives
/// against includePaths (quoted paths are also tried relative to the .fml
/// file's directory), builds the symbol and type tables, type-checks all
/// expressions, validates the anchors (keys, conflicts, sibling order) and
/// detects dependency cycles (bindings and anchors together).
/// Throws Fml::Error on the first error; appends to result.warnings.
void analyzeFile(const File& file, const std::vector<std::string>& includePaths,
                 SemaResult& result);

/// Deterministic dump of the semantic analysis result (snapshot tests).
void dumpSema(const File& file, const SemaResult& result, std::ostream& out);

} // namespace Fml
} // namespace Firmir
