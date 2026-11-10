#pragma once

// Firmir::Gui::Theme — application-wide visual constants (colors, font and
// item metrics, paddings, opacities) exposed as regular Firmir properties.
//
// Every value is a *writable* property: applications adjust them at runtime
// (e.g. from a config file) with setProperty() or from Firml
// (`Theme.highlightColor: "#FFA000"`), and each change emits
// propertyChanged so bindings re-evaluate and controls repaint.
//
// The instance is a process-wide singleton registered under the name
// "Firmir.Gui.Theme" (Object::singleton / Firml `Theme.<prop>`).
//
// Persistent customization: loadFromFile()/saveToFile() read/write a JSON
// object mapping property names to values (colors as "#RRGGBB"). At
// construction the theme loads the file named by the FIRMIR_THEME_CONFIG
// environment variable when it is set.

#include <firmir/gui/painter.hpp>
#include <firmir/object.hpp>
#include <firmir/string.hpp>

#include <memory>

// Property list shared by the header (declarations) and theme.cpp
// (defaults + registration). X(name, type, default).
#define FIRMIR_THEME_PROPS(X) \
    X(buttonWidthExtraSmall, double, 116.0) \
    X(buttonWidthLarge, double, 328.0) \
    X(buttonWidthMedium, double, 240.0) \
    X(buttonWidthSmall, double, 176.0) \
    X(buttonWidthTiny, double, 76.0) \
    X(darkPrimaryColor, Color, (Color::rgb(0x1C, 0x1E, 0x24))) \
    X(darkSecondaryColor, Color, (Color::rgb(0x2A, 0x2D, 0x35))) \
    X(errorColor, Color, (Color::rgb(0xE0, 0x50, 0x50))) \
    X(fontFamily, String, (String("Sans"))) \
    X(fontFamilyHeading, String, (String("Sans"))) \
    X(fontSizeExtraLarge, int, 28) \
    X(fontSizeExtraSmall, int, 12) \
    X(fontSizeHuge, int, 34) \
    X(fontSizeLarge, int, 22) \
    X(fontSizeMedium, int, 18) \
    X(fontSizeSmall, int, 14) \
    X(fontSizeTiny, int, 10) \
    X(highlightBackgroundColor, Color, (Color::rgb(0x33, 0x41, 0x55))) \
    X(highlightBackgroundOpacity, float, 0.30f) \
    X(highlightColor, Color, (Color::rgb(0x4A, 0x90, 0xD9))) \
    X(highlightDimmerColor, Color, (Color::rgb(0x2F, 0x5A, 0x82))) \
    X(horizontalPageMargin, double, 16.0) \
    X(iconSizeExtraLarge, double, 64.0) \
    X(iconSizeExtraSmall, double, 16.0) \
    X(iconSizeLarge, double, 48.0) \
    X(iconSizeMedium, double, 32.0) \
    X(iconSizeSmall, double, 24.0) \
    X(iconSizeSmallPlus, double, 28.0) \
    X(itemSizeExtraLarge, double, 96.0) \
    X(itemSizeExtraSmall, double, 28.0) \
    X(itemSizeHuge, double, 128.0) \
    X(itemSizeLarge, double, 72.0) \
    X(itemSizeMedium, double, 56.0) \
    X(itemSizeSmall, double, 40.0) \
    X(lightPrimaryColor, Color, (Color::rgb(0xF5, 0xF7, 0xFA))) \
    X(lightSecondaryColor, Color, (Color::rgb(0xD8, 0xDD, 0xE5))) \
    X(opacityFaint, double, 0.15) \
    X(opacityHigh, double, 0.75) \
    X(opacityLow, double, 0.40) \
    X(paddingLarge, double, 16.0) \
    X(paddingMedium, double, 8.0) \
    X(paddingSmall, double, 4.0) \
    X(pixelRatio, double, 1.0) \
    X(primaryColor, Color, (Color::rgb(0xE8, 0xE8, 0xE8))) \
    X(secondaryColor, Color, (Color::rgb(0x90, 0x98, 0xA8))) \
    X(secondaryHighlightColor, Color, (Color::rgb(0x7F, 0xB3, 0xE8)))

namespace Firmir {
namespace Gui {

class Theme : public Object {
public:
    /// Active color scheme: whether the palette is light-on-dark (dark UI)
    /// or dark-on-light (light UI). Exposed as the String property
    /// "colorScheme" ("lightOnDark" / "darkOnLight").
    enum class ColorScheme { LightOnDark, DarkOnLight };

    /// Returns the process-wide theme instance (created on first use).
    static Theme* instance();

    ColorScheme colorScheme() const;
    void setColorScheme(ColorScheme scheme);
    String colorSchemeName() const;
    void setColorSchemeName(String name);

#define FIRMIR_THEME_PROP_DECL(name, type, def) \
    type name() const; \
    void set_##name(type value);
    FIRMIR_THEME_PROPS(FIRMIR_THEME_PROP_DECL)
#undef FIRMIR_THEME_PROP_DECL

    /// Control corner radius used by the gui-controls painting code
    /// (derived from paddingSmall, clamped to a sane range).
    double controlRadius() const;

    /// Loads theme values from a JSON config file: an object mapping
    /// property names to values (colors as "#RRGGBB"). Unknown keys and
    /// value/type mismatches are skipped with a warning. Returns false
    /// when the file cannot be read or parsed.
    bool loadFromFile(String path);
    /// Writes all theme properties to @p path as a JSON config file in the
    /// loadFromFile() format. Returns false when the file cannot be
    /// written.
    bool saveToFile(String path) const;

private:
    explicit Theme(Object* parent = nullptr);
    ~Theme() override;

    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
