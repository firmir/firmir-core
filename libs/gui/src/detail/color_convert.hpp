#pragma once

#include <firmir/gui/painter.hpp>
#include <firmir/string.hpp>

#include <any>
#include <stdexcept>
#include <string>

namespace Firmir {
namespace Gui {
namespace detail {

/// Converts a std::any property value to Color (exact Color, "#RRGGBB" as
/// std::string / Firmir::String / const char*).
inline Color anyToColor(const std::any& value) {
    if (const Color* color = std::any_cast<Color>(&value))
        return *color;
    if (const std::string* text = std::any_cast<std::string>(&value))
        return Color::fromString(*text);
    if (const String* text = std::any_cast<String>(&value))
        return Color::fromString(text->toStdString());
    if (const char* const* text = std::any_cast<const char*>(&value))
        return Color::fromString(*text);
    throw std::invalid_argument("expected a Color or '#RRGGBB'");
}

} // namespace detail
} // namespace Gui
} // namespace Firmir
