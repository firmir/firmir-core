#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>

namespace Firmir {
namespace Gui {

/**
 * @brief Item that fills its bounds with a solid color, optionally with
 * rounded corners and a border ring.
 *
 * Exposes "color", "radius", "border.color" and "border.width" through the
 * Object property system, so they can be driven by firmlc-generated code:
 * setProperty("color", ...) with a Color value or a "#RRGGBB" string.
 *
 * Rounded/bordered shapes are rasterized in software with antialiasing and
 * cached as an RGBA bitmap that is re-created only when the size or the shape
 * parameters change.
 */
class Rectangle : public Item {
public:
    explicit Rectangle(Object* parent = nullptr);
    ~Rectangle() override;

    Color color() const;
    void setColor(Color color);

    /// Corner radius in pixels (0 = square corners, the default).
    float radius() const;
    void setRadius(float radius);

    /// Border ring color (black by default) and width in pixels (0 = no
    /// border, the default). The border is drawn inside the item bounds.
    Color borderColor() const;
    void setBorderColor(Color color);
    float borderWidth() const;
    void setBorderWidth(float width);

protected:
    void paint(Painter& painter) override;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
