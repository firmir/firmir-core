#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/string.hpp>

namespace Firmir {
namespace Gui {

/**
 * @brief Text item (QML Text).
 *
 * Draws "text" (UTF-8) with the built-in 8x8 bitmap font (font8x8, Public
 * Domain — see Painter::drawText). "fontSize" is the requested glyph cell
 * height in pixels; the effective size is rounded up to a multiple of 8
 * (integer scaling, default 8).
 *
 * The item size is content-driven (QML implicitWidth/implicitHeight
 * semantics): setting "text"/"fontSize" derives width/height from the
 * rendered text extents, unless the dimension was explicitly assigned —
 * explicit writes stick. Anchors re-resolve on size changes.
 */
class Text : public Item {
public:
    explicit Text(Object* parent = nullptr);
    ~Text() override;

    String text() const;
    void setText(String text);
    Color color() const;
    void setColor(Color color);
    int fontSize() const;
    void setFontSize(int fontSize);

protected:
    void paint(Painter& painter) override;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
