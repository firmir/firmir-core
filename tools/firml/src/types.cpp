#include "types.hpp"

namespace Firmir {
namespace Fml {

namespace {

const TypeInfo itemType{"Item",
                        nullptr,
                        {{"x", ValueType::Number},
                         {"y", ValueType::Number},
                         {"width", ValueType::Number},
                         {"height", ValueType::Number},
                         {"implicitWidth", ValueType::Number},
                         {"implicitHeight", ValueType::Number},
                         {"childrenRect", ValueType::Rect, true},
                         {"transformOrigin", ValueType::Number},
                         {"rotation", ValueType::Number},
                         {"scale", ValueType::Number},
                         {"opacity", ValueType::Number},
                         {"visible", ValueType::Bool},
                         {"enabled", ValueType::Bool},
                         {"clip", ValueType::Bool},
                         {"z", ValueType::Number},
                         {"focus", ValueType::Bool},
                         {"activeFocus", ValueType::Bool, true},
                         {"activeFocusOnTab", ValueType::Bool},
                         {"focusPolicy", ValueType::Number}},
                        {{"update", {}}},
                        {},
                        false};

const TypeInfo rectangleType{"Rectangle", &itemType, {{"color", ValueType::Color}},
                             {}, {}, false};

const TypeInfo textType{"Text",
                        &itemType,
                        {{"text", ValueType::String},
                         {"color", ValueType::Color},
                         {"fontSize", ValueType::Number}},
                        {},
                        {},
                        false};

const TypeInfo buttonType{"Button",
                          &itemType,
                          {{"text", ValueType::String},
                           {"color", ValueType::Color},
                           {"textColor", ValueType::Color},
                           {"fontSize", ValueType::Number},
                           {"pressed", ValueType::Bool, true}},
                          {},
                          {{"clicked", {}},
                           {"pressed", {}},
                           {"released", {}}},
                          false};

const TypeInfo appType{"App",
                       nullptr,
                       {{"screenWidth", ValueType::Number, true},
                        {"screenHeight", ValueType::Number, true}},
                       {{"show", {}},
                        {"hide", {}},
                        {"quit", {}},
                        {"setTitle", {ValueType::String}}},
                       {},
                       false};

// Window: root-only object hosting the item tree (Firmir::Gui::Window).
const TypeInfo windowType{"Window",
                          nullptr,
                          {{"title", ValueType::String},
                           {"width", ValueType::Number},
                           {"height", ValueType::Number}},
                          {{"show", {}}, {"hide", {}}, {"close", {}}},
                          {},
                          false,
                          TypeKind::Window,
                          "Firmir::Gui::Window"};

// Timer: non-visual Firmir::Timer child; `interval` is in milliseconds,
// `running: true` starts the timer after the other settings are applied.
const TypeInfo timerType{"Timer",
                         nullptr,
                         {{"interval", ValueType::Number},
                          {"singleShot", ValueType::Bool},
                          {"running", ValueType::Bool}},
                         {{"start", {}}, {"stop", {}}},
                         {{"timeout", {}}},
                         false,
                         TypeKind::Timer,
                         "Firmir::Timer"};

// Column/Row/Grid: positioner items (Firmir::Gui::Column/Row/Grid).
const TypeInfo columnType{"Column",
                          &itemType,
                          {{"spacing", ValueType::Number}},
                          {},
                          {},
                          false};
const TypeInfo rowType{"Row",
                       &itemType,
                       {{"spacing", ValueType::Number}},
                       {},
                       {},
                       false};
const TypeInfo gridType{"Grid",
                        &itemType,
                        {{"spacing", ValueType::Number},
                         {"columns", ValueType::Number},
                         {"rows", ValueType::Number}},
                        {},
                        {},
                        false};

// Repeater: an inert item whose single child is the delegate template;
// `model` (a ListModel id or a number) is handled specially by sema — it is
// not an ordinary property.
const TypeInfo repeaterType{"Repeater", &itemType, {}, {}, {}, false};

// ListModel: non-visual list of role-value rows (Firmir::Gui::ListModel).
const TypeInfo listModelType{"ListModel",
                             nullptr,
                             {{"count", ValueType::Number, true}},
                             {},
                             {},
                             false,
                             TypeKind::Model,
                             "Firmir::Gui::ListModel"};

// ListElement: one row of a ListModel; only literal role assignments are
// allowed. No runtime object is created — rows compile into append() calls.
const TypeInfo listElementType{"ListElement",
                               nullptr,
                               {},
                               {},
                               {},
                               false,
                               TypeKind::ModelElement,
                               {}};

} // namespace

const char* valueTypeName(ValueType type) {
    switch (type) {
    case ValueType::Number: return "Number";
    case ValueType::String: return "String";
    case ValueType::Color: return "Color";
    case ValueType::Bool: return "Bool";
    case ValueType::Rect: return "Rect";
    }
    return "?";
}

const MethodInfo* TypeInfo::findMethod(const std::string& name) const {
    for (const TypeInfo* type = this; type; type = type->base)
        for (const MethodInfo& method : type->ownMethods)
            if (method.name == name)
                return &method;
    return nullptr;
}

const SignalInfo* TypeInfo::findSignal(const std::string& name) const {
    for (const TypeInfo* type = this; type; type = type->base)
        for (const SignalInfo& signal : type->ownSignals)
            if (signal.name == name)
                return &signal;
    return nullptr;
}

const PropertyInfo* TypeInfo::findProperty(const std::string& name) const {
    for (const TypeInfo* type = this; type; type = type->base) {
        for (const PropertyInfo& prop : type->ownProperties)
            if (prop.name == name)
                return &prop;
    }
    return nullptr;
}

const TypeInfo* builtinType(const std::string& name) {
    if (name == "Item")
        return &itemType;
    if (name == "Rectangle")
        return &rectangleType;
    if (name == "Text")
        return &textType;
    if (name == "Button")
        return &buttonType;
    if (name == "Window")
        return &windowType;
    if (name == "Timer")
        return &timerType;
    if (name == "Column")
        return &columnType;
    if (name == "Row")
        return &rowType;
    if (name == "Grid")
        return &gridType;
    if (name == "Repeater")
        return &repeaterType;
    if (name == "ListModel")
        return &listModelType;
    if (name == "ListElement")
        return &listElementType;
    return nullptr;
}

const TypeInfo* builtinSingleton(const std::string& name) {
    if (name == "Firmir.Gui.App" || name == "App")
        return &appType;
    return nullptr;
}

bool lookupBuiltinFunction(const std::string& name, BuiltinFunction& out) {
    if (name == "min") {
        out = BuiltinFunction::Min;
        return true;
    }
    if (name == "max") {
        out = BuiltinFunction::Max;
        return true;
    }
    if (name == "lighter") {
        out = BuiltinFunction::Lighter;
        return true;
    }
    if (name == "darker") {
        out = BuiltinFunction::Darker;
        return true;
    }
    return false;
}

} // namespace Fml
} // namespace Firmir
