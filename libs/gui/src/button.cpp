#include <firmir/gui/button.hpp>

#include "detail/color_convert.hpp"
#include "detail/fontengine.hpp"

#include <algorithm>

namespace Firmir {
namespace Gui {

class Button::Impl {
public:
    int scale() const { return std::max(1, (fontSize + 7) / 8); }

    // The label is rasterized with the native font stack (default family:
    // Inter); the built-in 8x8 bitmap font remains the fallback when the
    // font stack is unavailable.
    void updateLabel(Button* q) {
        bitmap = detail::rasterizeText(text.toStdString(), font);
        ++generation;
        if (bitmap.valid) {
            q->setImplicitWidth(std::max(48.0f, bitmap.width + 16.0f));
            q->setImplicitHeight(std::max(24.0f, bitmap.height + 8.0f));
            return;
        }
        float w = 0, h = 0;
        Painter::textSize(text.toStdString(), scale(), w, h);
        q->setImplicitWidth(std::max(48.0f, w + 16));
        q->setImplicitHeight(std::max(24.0f, h + 8));
    }

    String text;
    Color color = Color::rgb(48, 48, 48);
    Color textColor = Color::rgb(255, 255, 255);
    Font font{kDefaultFontFamily, 16};
    detail::RasterizedText bitmap;
    std::uint64_t generation = 0;
    int fontSize = 16;
    bool pressed = false;
};

Button::Button(Object* parent) : Item(parent), d(std::make_unique<Impl>()) {
    setActiveFocusOnTab(true);
    registerProperty<String>("text", &Button::text, &Button::setText);
    registerPropertyImpl(
        "color", typeid(Color),
        [](const Object* object) -> std::any {
            return static_cast<const Button*>(object)->color();
        },
        [](Object* object, const std::any& value) {
            static_cast<Button*>(object)->setColor(detail::anyToColor(value));
        });
    registerPropertyImpl(
        "textColor", typeid(Color),
        [](const Object* object) -> std::any {
            return static_cast<const Button*>(object)->textColor();
        },
        [](Object* object, const std::any& value) {
            static_cast<Button*>(object)->setTextColor(detail::anyToColor(value));
        });
    registerProperty<int>("fontSize", &Button::fontSize, &Button::setFontSize);
    registerPropertyImpl(
        "pressed", typeid(bool),
        [](const Object* object) -> std::any {
            return static_cast<const Button*>(object)->isPressed();
        },
        [](Object*, const std::any&) {
            throw std::invalid_argument("pressed is read-only");
        });
    d->updateLabel(this);
}

Button::~Button() = default;

String Button::text() const { return d->text; }
void Button::setText(String text) {
    auto sceneLock = lockScene();
    if (d->text == text)
        return;
    d->text = std::move(text);
    d->updateLabel(this);
    update();
    notifyPropertyChanged("text");
}

Color Button::color() const { return d->color; }
void Button::setColor(Color color) {
    auto sceneLock = lockScene();
    if (d->color == color)
        return;
    d->color = color;
    update();
    notifyPropertyChanged("color");
}

Color Button::textColor() const { return d->textColor; }
void Button::setTextColor(Color color) {
    auto sceneLock = lockScene();
    if (d->textColor == color)
        return;
    d->textColor = color;
    update();
    notifyPropertyChanged("textColor");
}

int Button::fontSize() const { return d->fontSize; }
void Button::setFontSize(int fontSize) {
    auto sceneLock = lockScene();
    if (d->fontSize == fontSize)
        return;
    d->fontSize = fontSize;
    d->font.setPixelSize(fontSize);
    d->updateLabel(this);
    update();
    notifyPropertyChanged("fontSize");
}

bool Button::isPressed() const { return d->pressed; }

void Button::setPressed(bool pressed) {
    auto sceneLock = lockScene();
    if (d->pressed == pressed)
        return;
    d->pressed = pressed;
    update();
    notifyPropertyChanged("pressed");
}

void Button::click() { emitSignal(signalClicked); }

void Button::paint(Painter& painter) {
    painter.fillRect(RectF{0, 0, width(), height()},
                     d->pressed ? d->color.darker(1.5) : d->color);
    if (d->bitmap.valid) {
        painter.drawRgba(RectF{(width() - d->bitmap.width) / 2,
                               (height() - d->bitmap.height) / 2,
                               static_cast<float>(d->bitmap.width),
                               static_cast<float>(d->bitmap.height)},
                         d->bitmap.rgba.data(), d->bitmap.width,
                         d->bitmap.height, this, d->generation, d->textColor);
        return;
    }
    float w = 0, h = 0;
    const int scale = d->scale();
    Painter::textSize(d->text.toStdString(), scale, w, h);
    painter.drawText((width() - w) / 2, (height() - h) / 2,
                     d->text.toStdString(), d->textColor, scale);
}

void Button::mouseEvent(MouseEvent& event) {
    if (event.action == MouseEvent::Action::Press && event.button == 1) {
        event.accepted = true;
        setPressed(true);
        return;
    }
    if (event.action == MouseEvent::Action::Release && d->pressed) {
        event.accepted = true;
        setPressed(false);
        if (RectF{0, 0, width(), height()}.contains(event.pos.x, event.pos.y))
            click();
    }
}

void Button::touchEvent(TouchEvent& event) {
    if (event.action == TouchEvent::Action::Begin) {
        event.accepted = true;
        setPressed(true);
    } else if (event.action == TouchEvent::Action::End && d->pressed) {
        event.accepted = true;
        setPressed(false);
        if (!event.points.empty() &&
            RectF{0, 0, width(), height()}.contains(event.points[0].pos.x,
                                                    event.points[0].pos.y))
            click();
    }
}

void Button::keyEvent(KeyEvent& event) {
    // evdev KEY_ENTER=28, KEY_SPACE=57.
    if ((event.key == 28 || event.key == 57) && event.pressed) {
        event.accepted = true;
        click();
    }
}

} // namespace Gui
} // namespace Firmir
