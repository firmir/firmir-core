#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Firmir {
namespace Gui {

/// The family used when a Font has no explicit family set.
inline constexpr const char* kDefaultFontFamily = "Inter";

/// Value description of a font, independent from a particular render device.
class Font {
public:
    enum Weight { Thin = 100, ExtraLight = 200, Light = 300, Normal = 400,
                  Medium = 500, DemiBold = 600, Bold = 700, ExtraBold = 800,
                  Black = 900 };
    enum Style { StyleNormal, StyleItalic, StyleOblique };

    Font();
    explicit Font(std::string family, int pixelSize = 16,
                  int weight = Normal, bool italic = false);
    Font(const Font& other);
    Font& operator=(const Font& other);
    Font(Font&&) noexcept;
    Font& operator=(Font&&) noexcept;
    ~Font();

    const std::string& family() const;
    void setFamily(std::string family);
    std::string styleName() const;
    void setStyleName(std::string styleName);
    int pixelSize() const;
    void setPixelSize(int pixels);
    float pointSize() const;
    void setPointSize(float points);
    int effectivePixelSize() const;
    int weight() const;
    void setWeight(int weight);
    bool bold() const;
    void setBold(bool enabled);
    Style style() const;
    void setStyle(Style style);
    bool italic() const;
    void setItalic(bool enabled);
    bool underline() const;
    void setUnderline(bool enabled);
    bool strikeOut() const;
    void setStrikeOut(bool enabled);
    float letterSpacing() const;
    void setLetterSpacing(float pixels);

    /// Stable key used by text/texture caches.
    std::string cacheKey() const;
    friend bool operator==(const Font& lhs, const Font& rhs) {
        return lhs.cacheKey() == rhs.cacheKey();
    }
    friend bool operator!=(const Font& lhs, const Font& rhs) { return !(lhs == rhs); }
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

/// CPU RGBA representation of a fully shaped UTF-8 text run. Glyphs are
/// white; callers tint them when drawing.
struct RasterizedText {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    bool valid = false;
};

/// Shapes and rasterizes @p utf8 with @p font (Fontconfig/FreeType/HarfBuzz).
/// An empty family resolves to kDefaultFontFamily. Returns an invalid bitmap
/// when the font stack is unavailable.
RasterizedText rasterizeText(const std::string& utf8, const Font& font);

} // namespace Gui
} // namespace Firmir
