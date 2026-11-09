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

const TypeInfo rectangleType{"Rectangle",
                             &itemType,
                             {{"color", ValueType::Color},
                              {"radius", ValueType::Number},
                              {"border.color", ValueType::Color},
                              {"border.width", ValueType::Number}},
                             {},
                             {},
                             false};

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
                        {"setTitle", {ValueType::String}},
                        {"showMinimized", {}},
                        {"showNormal", {}},
                        {"showMaximized", {}},
                        {"toggleMaximized", {}},
                        {"close", {}}},
                       {},
                       false};

// Theme: process-wide visual constants (Firmir::Gui::Theme singleton).
// All properties are writable; changes emit propertyChanged so bindings
// re-evaluate and the affected controls repaint.
const TypeInfo themeType{"Theme",
                         nullptr,
                         {
                          { "buttonWidthExtraSmall", ValueType::Number },
                          { "buttonWidthLarge", ValueType::Number },
                          { "buttonWidthMedium", ValueType::Number },
                          { "buttonWidthSmall", ValueType::Number },
                          { "buttonWidthTiny", ValueType::Number },
                          { "horizontalPageMargin", ValueType::Number },
                          { "opacityFaint", ValueType::Number },
                          { "opacityHigh", ValueType::Number },
                          { "opacityLow", ValueType::Number },
                          { "paddingLarge", ValueType::Number },
                          { "paddingMedium", ValueType::Number },
                          { "paddingSmall", ValueType::Number },
                          { "pixelRatio", ValueType::Number },
                          { "iconSizeExtraLarge", ValueType::Number },
                          { "iconSizeExtraSmall", ValueType::Number },
                          { "iconSizeLarge", ValueType::Number },
                          { "iconSizeMedium", ValueType::Number },
                          { "iconSizeSmall", ValueType::Number },
                          { "iconSizeSmallPlus", ValueType::Number },
                          { "itemSizeExtraLarge", ValueType::Number },
                          { "itemSizeExtraSmall", ValueType::Number },
                          { "itemSizeHuge", ValueType::Number },
                          { "itemSizeLarge", ValueType::Number },
                          { "itemSizeMedium", ValueType::Number },
                          { "itemSizeSmall", ValueType::Number },
                          { "darkPrimaryColor", ValueType::Color },
                          { "darkSecondaryColor", ValueType::Color },
                          { "errorColor", ValueType::Color },
                          { "highlightBackgroundColor", ValueType::Color },
                          { "highlightColor", ValueType::Color },
                          { "highlightDimmerColor", ValueType::Color },
                          { "lightPrimaryColor", ValueType::Color },
                          { "lightSecondaryColor", ValueType::Color },
                          { "primaryColor", ValueType::Color },
                          { "secondaryColor", ValueType::Color },
                          { "secondaryHighlightColor", ValueType::Color },
                          { "colorScheme", ValueType::String },
                          { "fontFamily", ValueType::String },
                          { "fontFamilyHeading", ValueType::String },
                          { "fontSizeExtraLarge", ValueType::Number },
                          { "fontSizeExtraSmall", ValueType::Number },
                          { "fontSizeHuge", ValueType::Number },
                          { "fontSizeLarge", ValueType::Number },
                          { "fontSizeMedium", ValueType::Number },
                          { "fontSizeSmall", ValueType::Number },
                          { "fontSizeTiny", ValueType::Number },
                          { "highlightBackgroundOpacity", ValueType::Number }
                         },
                         {},
                         {},
                         false};

// Translator: runtime string translation (Firmir::Translator singleton,
// registered as "Firmir.Core.Translator"). trId() bindings depend on
// "language"; the methods manage the file set.
const TypeInfo translatorType{"Translator",
                              nullptr,
                              {{"language", ValueType::String}},
                              {{"setLanguage", {ValueType::String}},
                               {"addTranslationFile", {ValueType::String}},
                               {"removeTranslationFile", {ValueType::String}}},
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

// Connections/Binding: QML-style constructs targeting another object. They
// have no properties of their own (sema validates `target`/handlers and
// `target`/`property`/`value` against the target's type) and no runtime
// representation (codegen inlines them).
const TypeInfo connectionsType{"Connections", nullptr, {}, {}, {}, false,
                               TypeKind::Connections};
const TypeInfo bindingType{"Binding", nullptr, {}, {}, {}, false,
                           TypeKind::Binding};

// Component: a deferred item template (Firmir::Gui::Component). The child
// subtree is built per createObject() call; the only member is `id` plus the
// single template child.
const TypeInfo componentType{"Component",
                             nullptr,
                             {},
                             {{"createObject", {ValueType::Any}}},
                             {},
                             false,
                             TypeKind::Component,
                             "Firmir::Gui::Component"};

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

// Image: raster image item (Firmir::Gui::Image); fillMode is the string
// form ("Stretch", "PreserveAspectFit", "PreserveAspectCrop", "Pad");
// status is the numeric read-only load state (0 = Null, 2 = Ready,
// 3 = Error).
const TypeInfo imageType{"Image",
                         &itemType,
                         {{"source", ValueType::String},
                          {"fillMode", ValueType::String},
                          {"smooth", ValueType::Bool},
                          {"status", ValueType::Number}},
                         {},
                         {},
                         false,
                         TypeKind::Item,
                         "Firmir::Gui::Image"};

// MouseArea: pointer/touch input item (Firmir::Gui::MouseArea). The C++
// signals carry MouseEvent/Point (not Firml value types), so Firml code
// reacts through the read-only properties: onPressedChanged,
// onContainsMouseChanged, onMouseXChanged/onMouseYChanged. Only the
// argument-less signals are declared here.
const TypeInfo mouseAreaType{"MouseArea",
                             &itemType,
                             {{"hoverEnabled", ValueType::Bool},
                              {"mouseX", ValueType::Number, true},
                              {"mouseY", ValueType::Number, true},
                              {"pressed", ValueType::Bool, true},
                              {"containsMouse", ValueType::Bool, true}},
                             {},
                             {{"entered", {}},
                              {"exited", {}}},
                             false,
                             TypeKind::Item,
                             "Firmir::Gui::MouseArea"};

// Flickable: draggable content area (Firmir::Gui::Flickable); children go
// into contentItem().
const TypeInfo flickableType{"Flickable",
                             &itemType,
                             {{"contentWidth", ValueType::Number},
                              {"contentHeight", ValueType::Number}},
                             {},
                             {},
                             false,
                             TypeKind::Item,
                             "Firmir::Gui::Flickable",
                             /*contentItemParent=*/true};

// ShaderEffect: custom fragment shader quad (Firmir::Gui::ShaderEffect);
// the source image is wired from C++ (object pointers are not FML values).
const TypeInfo shaderEffectType{"ShaderEffect",
                                &itemType,
                                {{"fragmentShader", ValueType::String},
                                 {"color", ValueType::Color}},
                                {},
                                {},
                                false,
                                TypeKind::Item,
                                "Firmir::Gui::ShaderEffect"};

// EditableText: single/multi-line text input (Firmir::Gui::EditableText).
const TypeInfo editableTextType{"EditableText",
                                &itemType,
                                {{"text", ValueType::String},
                                 {"multiline", ValueType::Bool}},
                                {},
                                {{"accepted", {}}},
                                false,
                                TypeKind::Item,
                                "Firmir::Gui::EditableText"};

// ---- Controls (Firmir::Gui::Controls, the gui-controls library) ----

const TypeInfo checkBoxType{"CheckBox",
                            &itemType,
                            {{"text", ValueType::String},
                             {"checked", ValueType::Bool},
                             {"tristate", ValueType::Bool}},
                            {},
                            {{"clicked", {}}},
                            false,
                            TypeKind::Item,
                            "Firmir::Gui::Controls::CheckBox"};

const TypeInfo radioButtonType{"RadioButton",
                               &itemType,
                               {{"text", ValueType::String},
                                {"checked", ValueType::Bool}},
                               {},
                               {{"clicked", {}}},
                               false,
                               TypeKind::Item,
                               "Firmir::Gui::Controls::RadioButton"};

const TypeInfo switchType{"Switch",
                          &itemType,
                          {{"text", ValueType::String},
                           {"checked", ValueType::Bool}},
                          {},
                          {{"clicked", {}}},
                          false,
                          TypeKind::Item,
                          "Firmir::Gui::Controls::Switch"};

const TypeInfo progressBarType{"ProgressBar",
                               &itemType,
                               {{"value", ValueType::Number},
                                {"from", ValueType::Number},
                                {"to", ValueType::Number},
                                {"indeterminate", ValueType::Bool}},
                               {},
                               {},
                               false,
                               TypeKind::Item,
                               "Firmir::Gui::Controls::ProgressBar"};

const TypeInfo busyIndicatorType{"BusyIndicator",
                                 &itemType,
                                 {{"running", ValueType::Bool}},
                                 {},
                                 {},
                                 false,
                                 TypeKind::Item,
                                 "Firmir::Gui::Controls::BusyIndicator"};

const TypeInfo scrollViewType{"ScrollView",
                              &itemType,
                              {{"contentWidth", ValueType::Number},
                               {"contentHeight", ValueType::Number}},
                              {},
                              {},
                              false,
                              TypeKind::Item,
                              "Firmir::Gui::Controls::ScrollView",
                              /*contentItemParent=*/true};

const TypeInfo stackViewType{"StackView",
                             &itemType,
                             {{"depth", ValueType::Number, true}},
                             {},
                             {},
                             false,
                             TypeKind::Item,
                             "Firmir::Gui::Controls::StackView",
                             false,
                             "push"};

const TypeInfo swipeViewType{"SwipeView",
                             &itemType,
                             {{"currentIndex", ValueType::Number},
                              {"interactive", ValueType::Bool}},
                             {},
                             {},
                             false,
                             TypeKind::Item,
                             "Firmir::Gui::Controls::SwipeView",
                             false,
                             "addItem"};

const TypeInfo splitViewType{"SplitView",
                             &itemType,
                             {{"orientation", ValueType::String}},
                             {},
                             {},
                             false,
                             TypeKind::Item,
                             "Firmir::Gui::Controls::SplitView",
                             false,
                             "addItem"};

const TypeInfo tabBarType{"TabBar",
                          &itemType,
                          {{"currentIndex", ValueType::Number},
                           {"count", ValueType::Number, true}},
                          {{"addTab", {ValueType::String}}},
                          {},
                          false,
                          TypeKind::Item,
                          "Firmir::Gui::Controls::TabBar"};

} // namespace

const char* valueTypeName(ValueType type) {
    switch (type) {
    case ValueType::Number: return "Number";
    case ValueType::String: return "String";
    case ValueType::Color: return "Color";
    case ValueType::Bool: return "Bool";
    case ValueType::Rect: return "Rect";
    case ValueType::Any: return "Any";
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
    if (name == "Image")
        return &imageType;
    if (name == "MouseArea")
        return &mouseAreaType;
    if (name == "Flickable")
        return &flickableType;
    if (name == "ShaderEffect")
        return &shaderEffectType;
    if (name == "EditableText")
        return &editableTextType;
    if (name == "CheckBox")
        return &checkBoxType;
    if (name == "RadioButton")
        return &radioButtonType;
    if (name == "Switch")
        return &switchType;
    if (name == "ProgressBar")
        return &progressBarType;
    if (name == "BusyIndicator")
        return &busyIndicatorType;
    if (name == "ScrollView")
        return &scrollViewType;
    if (name == "StackView")
        return &stackViewType;
    if (name == "SwipeView")
        return &swipeViewType;
    if (name == "SplitView")
        return &splitViewType;
    if (name == "TabBar")
        return &tabBarType;
    if (name == "Connections")
        return &connectionsType;
    if (name == "Binding")
        return &bindingType;
    if (name == "Component")
        return &componentType;
    return nullptr;
}

const TypeInfo* builtinSingleton(const std::string& name) {
    if (name == "Firmir.Gui.App" || name == "App")
        return &appType;
    if (name == "Firmir.Gui.Theme" || name == "Theme")
        return &themeType;
    if (name == "Firmir.Core.Translator" || name == "Translator" ||
        name == "Firmir.Gui.Translator") // legacy name, pre-move to core
        return &translatorType;
    return nullptr;
}

/// The canonical dotted singleton name ("Firmir.Gui.Theme" for both "Theme"
/// and the dotted form), or "" when @p name is not a singleton.
std::string canonicalSingletonName(const std::string& name) {
    if (name == "Firmir.Gui.App" || name == "App")
        return "Firmir.Gui.App";
    if (name == "Firmir.Gui.Theme" || name == "Theme")
        return "Firmir.Gui.Theme";
    if (name == "Firmir.Core.Translator" || name == "Translator" ||
        name == "Firmir.Gui.Translator") // legacy name, pre-move to core
        return "Firmir.Core.Translator";
    return {};
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
    if (name == "trId") {
        out = BuiltinFunction::TrId;
        return true;
    }
    return false;
}

} // namespace Fml
} // namespace Firmir
