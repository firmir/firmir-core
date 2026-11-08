#pragma once

#include <string>
#include <vector>

namespace Firmir {
namespace Fml {

/// Value types of Firml expressions and item properties (v0.1).
enum class ValueType { Number, String, Color, Bool, Rect, Any };

/// Any: the statically unknown type of an opaque object's property (checked
/// at runtime through the dynamic property bag). Compatible with every
/// operator/operand combination; propagates through arithmetic.

const char* valueTypeName(ValueType type); // "Number", "String", ...

struct PropertyInfo {
    std::string name;
    ValueType type;
    bool readOnly = false;
};

struct MethodInfo {
    std::string name;
    std::vector<ValueType> parameters;
};

struct SignalInfo {
    std::string name;
    std::vector<ValueType> parameters;
};

/// Object categories known to firmlc. Items are the default; Window is only
/// allowed as the root object; Timer is a non-visual object (no children,
/// anchors or behaviors, static properties only). Model is a non-visual data
/// object (ListModel: only ListElement children); ModelElement (ListElement)
/// lives only inside a Model and carries literal role values.
enum class TypeKind {
    Item,
    Window,
    Timer,
    Model,
    ModelElement,
    // QML-style attachable constructs; they create no runtime object — sema
    // resolves their target and codegen inlines the connects/bindings.
    Connections,
    Binding
};

/// Static description of an item type known to firmlc. Types from project
/// headers are opaque: only the Item base properties are known for them.
struct TypeInfo {
    std::string name;
    const TypeInfo* base = nullptr; // single inheritance; Item is the root
    std::vector<PropertyInfo> ownProperties;
    std::vector<MethodInfo> ownMethods;
    std::vector<SignalInfo> ownSignals;
    bool opaque = false;
    TypeKind kind = TypeKind::Item;
    /// C++ class name when it differs from the default mapping
    /// (dotted -> ::-path, or "Firmir::Gui::<name>" for short builtins).
    std::string cppName = {};
    /// Children are parented to <obj>->contentItem() instead of the object
    /// itself (ScrollView, Flickable).
    bool contentItemParent = false;
    /// When non-empty, each direct child is additionally registered with the
    /// container through this method right after construction
    /// (StackView: "push", SwipeView/SplitView: "addItem").
    std::string childAddMethod = {};

    /// Finds a property walking up the inheritance chain (nullptr if absent).
    const PropertyInfo* findProperty(const std::string& name) const;
    const MethodInfo* findMethod(const std::string& name) const;
    const SignalInfo* findSignal(const std::string& name) const;
};

/// The types firmlc knows natively: Item (base), Rectangle, Text, Button,
/// Window (root only) and Timer (non-visual). Once the real firmir item
/// headers carry this meta, it will come from them; until then it is built
/// into the transpiler.
const TypeInfo* builtinType(const std::string& name);
/// Singleton types accessible by a fully-qualified Firml name.
const TypeInfo* builtinSingleton(const std::string& name);
/// The canonical dotted singleton name ("Firmir.Gui.Theme" for both "Theme"
/// and the dotted form), or "" when @p name is not a singleton.
std::string canonicalSingletonName(const std::string& name);

enum class BuiltinFunction { Min, Max, Lighter, Darker, TrId };

/// The builtin expression functions: min/max (Number, ...) -> Number,
/// lighter/darker (Color) -> Color, trId (String id) -> String (translated;
/// re-evaluates when the Translator language property changes).
bool lookupBuiltinFunction(const std::string& name, BuiltinFunction& out);

} // namespace Fml
} // namespace Firmir
