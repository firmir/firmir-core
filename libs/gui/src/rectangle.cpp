#include <firmir/gui/rectangle.hpp>

#include "detail/color_convert.hpp"

#include <firmir/string.hpp>

namespace Firmir {
namespace Gui {

class Rectangle::Impl {
public:
    Color color;
};

Rectangle::Rectangle(Object* parent) : Item(parent), d(std::make_unique<Impl>()) {
    registerPropertyImpl(
        "color", typeid(Color),
        [](const Object* object) -> std::any {
            return static_cast<const Rectangle*>(object)->color();
        },
        [](Object* object, const std::any& value) {
            static_cast<Rectangle*>(object)->setColor(detail::anyToColor(value));
        });
}

Rectangle::~Rectangle() = default;

Color Rectangle::color() const { return d->color; }

void Rectangle::setColor(Color color) {
    if (d->color == color)
        return;
    d->color = color;
    update();
    notifyPropertyChanged("color");
}

void Rectangle::paint(Painter& painter) {
    painter.fillRect(RectF{0, 0, width(), height()}, d->color);
}

} // namespace Gui
} // namespace Firmir
