#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>

namespace Firmir {
namespace Gui {

/**
 * @brief Item that fills its bounds with a solid color (QML Rectangle).
 *
 * Exposes "color" through the Object property system, so it can be driven by
 * firmlc-generated code: setProperty("color", ...) with a Color value or a
 * "#RRGGBB" string.
 */
class Rectangle : public Item {
public:
    explicit Rectangle(Object* parent = nullptr);
    ~Rectangle() override;

    Color color() const;
    void setColor(Color color);

protected:
    void paint(Painter& painter) override;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
