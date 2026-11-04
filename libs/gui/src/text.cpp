#include <firmir/gui/text.hpp>

#include "detail/color_convert.hpp"

#include <algorithm>
#include <utility>

namespace Firmir {
namespace Gui {

class Text::Impl {
public:
    /// Integer glyph scaling for the 8x8 bitmap font.
    int scale() const { return std::max(1, (fontSize + 7) / 8); }
    /// Content-driven implicit size (QML implicitWidth/implicitHeight).
    void updateImplicitSize(Text* q) {
        float w = 0, h = 0;
        Painter::textSize(text.toStdString(), scale(), w, h);
        q->setImplicitWidth(w);
        q->setImplicitHeight(h);
    }

    String text;
    Color color;
    int fontSize = 8;
};

Text::Text(Object* parent) : Item(parent), d(std::make_unique<Impl>()) {
    registerProperty<String>("text", &Text::text, &Text::setText);
    registerPropertyImpl(
        "color", typeid(Color),
        [](const Object* object) -> std::any {
            return static_cast<const Text*>(object)->color();
        },
        [](Object* object, const std::any& value) {
            static_cast<Text*>(object)->setColor(detail::anyToColor(value));
        });
    registerProperty<int>("fontSize", &Text::fontSize, &Text::setFontSize);

}

Text::~Text() = default;

String Text::text() const { return d->text; }

void Text::setText(String text) {
    if (d->text == text)
        return;
    d->text = std::move(text);
    d->updateImplicitSize(this);
    update();
    notifyPropertyChanged("text");
}

Color Text::color() const { return d->color; }

void Text::setColor(Color color) {
    if (d->color == color)
        return;
    d->color = color;
    update();
    notifyPropertyChanged("color");
}

int Text::fontSize() const { return d->fontSize; }

void Text::setFontSize(int fontSize) {
    if (d->fontSize == fontSize)
        return;
    d->fontSize = fontSize;
    d->updateImplicitSize(this);
    update();
    notifyPropertyChanged("fontSize");
}

void Text::paint(Painter& painter) {
    if (d->text.isEmpty())
        return;
    painter.drawText(0, 0, d->text.toStdString(), d->color, d->scale());
}

} // namespace Gui
} // namespace Firmir
