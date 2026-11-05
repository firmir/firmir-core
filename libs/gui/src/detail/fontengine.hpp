#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Firmir {
namespace Gui {

class Font;

namespace detail {

/// CPU RGBA representation of a fully shaped UTF-8 text run. The GUI item
/// uploads this bitmap through Painter's device-lifetime texture cache.
struct RasterizedText {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    bool valid = false;
};

RasterizedText rasterizeText(const std::string& utf8, const Font& font);

} // namespace detail
} // namespace Gui
} // namespace Firmir
