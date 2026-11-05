#pragma once

#include <firmir/gui/font.hpp>
#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/string.hpp>

namespace Firmir {
namespace Gui {

/// UTF-8 text item with legacy bitmap-font compatibility and native
/// FreeType/HarfBuzz rendering when Font is assigned.
class Text : public Item {
public:
    explicit Text(Object* parent = nullptr);
    ~Text() override;

    String text() const;
    void setText(String text);
    Color color() const;
    void setColor(Color color);
    Font font() const;
    void setFont(Font font);
    /// Legacy QML-like alias for font.pixelSize.
    int fontSize() const;
    void setFontSize(int fontSize);
    String fontFamily() const;
    void setFontFamily(String family);
    int fontPixelSize() const;
    void setFontPixelSize(int pixels);
    float fontPointSize() const;
    void setFontPointSize(float points);
    bool fontBold() const;
    void setFontBold(bool bold);
    bool fontItalic() const;
    void setFontItalic(bool italic);
    bool fontUnderline() const;
    void setFontUnderline(bool underline);
    bool fontStrikeOut() const;
    void setFontStrikeOut(bool strikeOut);

protected:
    void paint(Painter& painter) override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
