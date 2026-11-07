#include <firmir/gui/rectangle.hpp>

#include "detail/color_convert.hpp"

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

namespace {

/// Rasterizes a rounded rect with a border ring into straight-alpha RGBA8.
/// The signed distance field gives ~1 px analytic antialiasing on the outer
/// edge and on the fill/border boundary.
void rasterizeRoundedRect(int width, int height, float radius,
                          float borderWidth, Color fill, Color border,
                          std::vector<std::uint8_t>& out) {
    out.assign(static_cast<std::size_t>(width) * height * 4, 0);
    const float r = std::min(radius, std::min(width, height) / 2.0f);
    const float cx = width / 2.0f, cy = height / 2.0f;
    const float hx = cx - r, hy = cy - r;
    const float fillA = fill.a / 255.0f, borderA = border.a / 255.0f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float qx = std::fabs(px - cx) - hx;
            const float qy = std::fabs(py - cy) - hy;
            const float ax = std::max(qx, 0.0f), ay = std::max(qy, 0.0f);
            // Signed distance to the rounded box (< 0 inside).
            const float d = std::sqrt(ax * ax + ay * ay) +
                            std::min(std::max(qx, qy), 0.0f) - r;
            const float outer = std::clamp(0.5f - d, 0.0f, 1.0f);
            if (outer <= 0.0f)
                continue;
            float fillCov = outer, borderCov = 0.0f;
            if (borderWidth > 0.0f) {
                fillCov = std::clamp(0.5f - (d + borderWidth), 0.0f, 1.0f);
                borderCov = outer - fillCov;
            }
            // Fill over border, both scaled by their coverage.
            const float aF = fillA * fillCov;
            const float aB = borderA * borderCov * (1.0f - aF);
            const float alpha = aF + aB;
            if (alpha <= 0.0f)
                continue;
            auto channel = [&](std::uint8_t f, std::uint8_t b) {
                return static_cast<std::uint8_t>(
                    std::lround((f * aF + b * aB) / alpha));
            };
            std::uint8_t* p =
                out.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            p[0] = channel(fill.r, border.r);
            p[1] = channel(fill.g, border.g);
            p[2] = channel(fill.b, border.b);
            p[3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
        }
    }
}

} // namespace

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
        rasterizeRoundedRect(bitmapWidth, bitmapHeight, d->radius,
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
