#include <firmir/gui/rectangle.hpp>

#include "detail/color_convert.hpp"
#include "detail/rounded_rect.hpp"

#include <firmir/string.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Firmir {
namespace Gui {

class Rectangle::Impl {
public:
    Color color;
    float radius = 0.0f;
    Color borderColor; // opaque black, like the Color default
    float borderWidth = 0.0f;

    // Software-rasterized rounded/bordered appearance, drawn through the
    // Painter's RGBA texture cache (keyed by the owner Rectangle; re-uploaded
    // only when the generation changes).
    std::vector<std::uint8_t> bitmap;
    int bitmapWidth = 0;
    int bitmapHeight = 0;
    std::uint64_t generation = 0;
    bool dirty = true;
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
    registerProperty<float>("radius", &Rectangle::radius, &Rectangle::setRadius);
    registerPropertyImpl(
        "border.color", typeid(Color),
        [](const Object* object) -> std::any {
            return static_cast<const Rectangle*>(object)->borderColor();
        },
        [](Object* object, const std::any& value) {
            static_cast<Rectangle*>(object)->setBorderColor(
                detail::anyToColor(value));
        });
    registerProperty<float>("border.width", &Rectangle::borderWidth,
                            &Rectangle::setBorderWidth);
}

Rectangle::~Rectangle() = default;

Color Rectangle::color() const { return d->color; }

void Rectangle::setColor(Color color) {
    if (d->color == color)
        return;
    d->color = color;
    d->dirty = true;
    update();
    notifyPropertyChanged("color");
}

float Rectangle::radius() const { return d->radius; }

void Rectangle::setRadius(float radius) {
    radius = std::max(0.0f, radius);
    if (d->radius == radius)
        return;
    d->radius = radius;
    d->dirty = true;
    update();
    notifyPropertyChanged("radius");
}

Color Rectangle::borderColor() const { return d->borderColor; }

void Rectangle::setBorderColor(Color color) {
    if (d->borderColor == color)
        return;
    d->borderColor = color;
    d->dirty = true;
    update();
    notifyPropertyChanged("border.color");
}

float Rectangle::borderWidth() const { return d->borderWidth; }

void Rectangle::setBorderWidth(float width) {
    width = std::max(0.0f, width);
    if (d->borderWidth == width)
        return;
    d->borderWidth = width;
    d->dirty = true;
    update();
    notifyPropertyChanged("border.width");
}

void Rectangle::paint(Painter& painter) {
    const float w = width(), h = height();
    if (d->radius <= 0.0f && d->borderWidth <= 0.0f) {
        painter.fillRect(RectF{0, 0, w, h}, d->color);
        return;
    }
    const int bitmapWidth = std::max(1, static_cast<int>(std::lround(w)));
    const int bitmapHeight = std::max(1, static_cast<int>(std::lround(h)));
    if (d->dirty || bitmapWidth != d->bitmapWidth ||
        bitmapHeight != d->bitmapHeight) {
        detail::rasterizeRoundedRect(bitmapWidth, bitmapHeight, d->radius,
                             d->borderWidth, d->color, d->borderColor,
                             d->bitmap);
        d->bitmapWidth = bitmapWidth;
        d->bitmapHeight = bitmapHeight;
        d->dirty = false;
        ++d->generation;
    }
    painter.drawRgba(RectF{0, 0, w, h}, d->bitmap.data(), bitmapWidth,
                     bitmapHeight, this, d->generation);
}

} // namespace Gui
} // namespace Firmir
