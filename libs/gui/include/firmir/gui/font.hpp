#pragma once

#include <string>

namespace Firmir {
namespace Gui {

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

    const std::string& family() const { return m_family; }
    void setFamily(std::string family);
    std::string styleName() const { return m_styleName; }
    void setStyleName(std::string styleName) { m_styleName = std::move(styleName); }
    int pixelSize() const { return m_pixelSize; }
    void setPixelSize(int pixels);
    float pointSize() const { return m_pointSize; }
    void setPointSize(float points);
    int effectivePixelSize() const;
    int weight() const { return m_weight; }
    void setWeight(int weight) { m_weight = weight; }
    bool bold() const { return m_weight >= DemiBold; }
    void setBold(bool enabled) { m_weight = enabled ? Bold : Normal; }
    Style style() const { return m_style; }
    void setStyle(Style style) { m_style = style; }
    bool italic() const { return m_style != StyleNormal; }
    void setItalic(bool enabled) { m_style = enabled ? StyleItalic : StyleNormal; }
    bool underline() const { return m_underline; }
    void setUnderline(bool enabled) { m_underline = enabled; }
    bool strikeOut() const { return m_strikeOut; }
    void setStrikeOut(bool enabled) { m_strikeOut = enabled; }
    float letterSpacing() const { return m_letterSpacing; }
    void setLetterSpacing(float pixels) { m_letterSpacing = pixels; }

    /// Stable key used by text/texture caches.
    std::string cacheKey() const;
    friend bool operator==(const Font& lhs, const Font& rhs) {
        return lhs.cacheKey() == rhs.cacheKey();
    }
    friend bool operator!=(const Font& lhs, const Font& rhs) { return !(lhs == rhs); }
private:
    std::string m_family;
    std::string m_styleName;
    int m_pixelSize = 16;
    float m_pointSize = -1.0f;
    int m_weight = Normal;
    Style m_style = StyleNormal;
    float m_letterSpacing = 0.0f;
    bool m_underline = false;
    bool m_strikeOut = false;
};

} // namespace Gui
} // namespace Firmir
