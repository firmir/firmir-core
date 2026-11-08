#include <firmir/gui/theme.hpp>

#include "detail/color_convert.hpp"

#include <firmir/json.hpp>
#include <firmir/log.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <unordered_set>

namespace Firmir {
namespace Gui {

namespace {

/// Names of the registered theme properties — loadFromFile() only accepts
/// these (a typo'd key must not create a dynamic property instead).
const std::unordered_set<std::string>& themePropertyNames() {
    static const std::unordered_set<std::string> names = {
#define FIRMIR_THEME_NAME(name, type, def) #name,
        FIRMIR_THEME_PROPS(FIRMIR_THEME_NAME)
#undef FIRMIR_THEME_NAME
        "colorScheme",
    };
    return names;
}

/// "#RRGGBB" (or "#AARRGGBB" when the color is translucent).
std::string colorToHex(const Color& color) {
    char text[16];
    if (color.a == 255)
        std::snprintf(text, sizeof(text), "#%02X%02X%02X", color.r, color.g,
                      color.b);
    else
        std::snprintf(text, sizeof(text), "#%02X%02X%02X%02X", color.a,
                      color.r, color.g, color.b);
    return text;
}

/// Registers one theme property; Color properties additionally accept
/// "#RRGGBB" strings through detail::anyToColor().
template <typename T>
void registerThemeProperty(Theme* theme, const char* name, T (Theme::*getter)() const,
                           void (Theme::*setter)(T)) {
    if constexpr (std::is_same_v<T, Color>) {
        theme->registerPropertyImpl(
            name, typeid(Color),
            [getter](const Object* object) -> std::any {
                return (static_cast<const Theme*>(object)->*getter)();
            },
            [setter](Object* object, const std::any& value) {
                (static_cast<Theme*>(object)->*setter)(
                    detail::anyToColor(value));
            });
    } else {
        theme->registerProperty<T>(name, getter, setter);
    }
}

} // namespace

Theme::Theme(Object* parent) : Object(parent) {
    // Register every macro-declared property so it is visible to
    // setProperty()/getProperty() and to the Firml property system.
#define FIRMIR_THEME_PROP_REG(name, type, def) \
    registerThemeProperty<type>(this, #name, &Theme::name, &Theme::set_##name);
    FIRMIR_THEME_PROPS(FIRMIR_THEME_PROP_REG)
#undef FIRMIR_THEME_PROP_REG
    registerProperty<String>("colorScheme", &Theme::colorSchemeName,
                             &Theme::setColorSchemeName);

    // Optional persistent customization.
    if (const char* config = std::getenv("FIRMIR_THEME_CONFIG"))
        if (*config)
            loadFromFile(String(config));
}

Theme* Theme::instance() {
    static Theme* theme = [] {
        if (auto* existing = Object::singleton<Theme>("Firmir.Gui.Theme"))
            return existing;
        auto* created = new Theme;
        Object::registerSingleton("Firmir.Gui.Theme", created);
        return created;
    }();
    return theme;
}

void Theme::setColorScheme(ColorScheme scheme) {
    if (m_colorScheme == scheme)
        return;
    m_colorScheme = scheme;
    notifyPropertyChanged("colorScheme");
}

String Theme::colorSchemeName() const {
    return m_colorScheme == ColorScheme::LightOnDark ? String("lightOnDark")
                                                     : String("darkOnLight");
}

void Theme::setColorSchemeName(String name) {
    if (name == String("lightOnDark"))
        setColorScheme(ColorScheme::LightOnDark);
    else if (name == String("darkOnLight"))
        setColorScheme(ColorScheme::DarkOnLight);
    else
        logWarning() << "Theme: unknown colorScheme" << name;
}

#define FIRMIR_THEME_PROP_IMPL(name, type, def) \
    void Theme::set_##name(type value) { \
        if (m_##name == value) \
            return; \
        m_##name = value; \
        notifyPropertyChanged(#name); \
    }
FIRMIR_THEME_PROPS(FIRMIR_THEME_PROP_IMPL)
#undef FIRMIR_THEME_PROP_IMPL

double Theme::controlRadius() const {
    return std::clamp(m_paddingSmall * 1.5, 2.0, 12.0);
}

bool Theme::loadFromFile(String path) {
    std::ifstream in(path.toStdString());
    if (!in) {
        logWarning() << "Theme: cannot open config" << path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    JsonDocument::ParseError parseError;
    const JsonDocument doc = JsonDocument::fromJson(
        ByteArray(text.data(), static_cast<int>(text.size())), &parseError);
    if (parseError.error != JsonDocument::ParseError::NoError ||
        !doc.isObject()) {
        logWarning() << "Theme: bad JSON in config" << path;
        return false;
    }
    const JsonObject root = doc.object();
    for (const String& key : root.keys()) {
        const std::string name = key.toStdString();
        if (!themePropertyNames().count(name)) {
            logWarning() << "Theme: ignoring unknown config key" << key;
            continue;
        }
        const JsonValue value = root.value(key);
        try {
            if (value.isDouble())
                setProperty(name, std::any(value.toDouble()));
            else if (value.isBool())
                setProperty(name, std::any(value.toBool()));
            else if (value.isString())
                setProperty(name, std::any(value.toString().toStdString()));
            else
                logWarning() << "Theme: unsupported value type for" << key;
        } catch (const std::invalid_argument&) {
            logWarning() << "Theme: bad value for config key" << key;
        }
    }
    logInfo() << "Theme: loaded config" << path;
    return true;
}

bool Theme::saveToFile(String path) const {
    JsonObject root;
    for (const PropertyMeta& meta : propertyMetaList()) {
        if (!themePropertyNames().count(meta.name))
            continue; // dynamic properties are not theme values
        const std::any value = getProperty(meta.name);
        if (const auto* v = std::any_cast<Color>(&value))
            root.insert(String(meta.name), JsonValue(String(colorToHex(*v))));
        else if (const auto* v = std::any_cast<double>(&value))
            root.insert(String(meta.name), JsonValue(*v));
        else if (const auto* v = std::any_cast<float>(&value))
            root.insert(String(meta.name), JsonValue(static_cast<double>(*v)));
        else if (const auto* v = std::any_cast<int>(&value))
            root.insert(String(meta.name), JsonValue(*v));
        else if (const auto* v = std::any_cast<bool>(&value))
            root.insert(String(meta.name), JsonValue(*v));
        else if (const auto* v = std::any_cast<String>(&value))
            root.insert(String(meta.name), JsonValue(*v));
    }
    JsonDocument doc;
    doc.setObject(root);
    const ByteArray json = doc.toJson(JsonDocument::JsonFormat::Indented);
    std::ofstream out(path.toStdString(), std::ios::binary | std::ios::trunc);
    if (!out) {
        logWarning() << "Theme: cannot write config" << path;
        return false;
    }
    out.write(json.constData(), json.size());
    return out.good();
}

} // namespace Gui
} // namespace Firmir
