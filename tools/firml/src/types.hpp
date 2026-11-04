#pragma once

#include <string>
#include <vector>

namespace Firmir {
namespace Fml {

/// Value types of Firml expressions and item properties (v0.1).
enum class ValueType { Number, String, Color, Bool, Rect };

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

/// Static description of an item type known to firmlc. Types from project
/// headers are opaque: only the Item base properties are known for them.
struct TypeInfo {
    std::string name;
    const TypeInfo* base = nullptr; // single inheritance; Item is the root
    std::vector<PropertyInfo> ownProperties;
    std::vector<MethodInfo> ownMethods;
    std::vector<SignalInfo> ownSignals;
    bool opaque = false;

    /// Finds a property walking up the inheritance chain (nullptr if absent).
    const PropertyInfo* findProperty(const std::string& name) const;
    const MethodInfo* findMethod(const std::string& name) const;
    const SignalInfo* findSignal(const std::string& name) const;
};

/// The types firmlc knows natively: Item (base), Rectangle, Text. Once the
/// real firmir item headers exist, this meta will come from them; until then
/// it is built into the transpiler.
const TypeInfo* builtinType(const std::string& name);
/// Singleton types accessible by a fully-qualified Firml name.
const TypeInfo* builtinSingleton(const std::string& name);

enum class BuiltinFunction { Min, Max, Lighter, Darker };

/// The builtin expression functions: min/max (Number, ...) -> Number,
/// lighter/darker (Color) -> Color.
bool lookupBuiltinFunction(const std::string& name, BuiltinFunction& out);

} // namespace Fml
} // namespace Firmir
