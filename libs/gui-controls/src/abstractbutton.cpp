#include <firmir/gui-controls/abstractbutton.hpp>

#include <firmir/gui/theme.hpp>

#include <algorithm>

namespace Firmir {
namespace Gui {
namespace Controls {

class AbstractButton::Impl {
public:
    String text;
    Font font{kDefaultFontFamily, 16};
    RasterizedText label;
    std::uint64_t labelGeneration = 0;
    bool pressed = false;
    bool checked = false;
    bool checkable = false;
};

class Button::Impl {
public:
    bool flat = false;
};

class CheckBox::Impl {
public:
    CheckState checkState = CheckState::Unchecked;
    bool tristate = false;
};

class RadioButton::Impl {
public:
    bool autoExclusive = true;
};

AbstractButton::AbstractButton(Object* parent) : Item(parent), d(std::make_unique<Impl>()) {
    setActiveFocusOnTab(true);
    registerProperty<String>("text", &AbstractButton::text, &AbstractButton::setText);
    registerProperty<Font>("font", &AbstractButton::font, &AbstractButton::setFont);
    registerProperty<bool>("checked", &AbstractButton::isChecked, &AbstractButton::setChecked);
    registerProperty<bool>("checkable", &AbstractButton::checkable, &AbstractButton::setCheckable);
    registerPropertyImpl("pressed", typeid(bool),
        [](const Object* o) -> std::any { return static_cast<const AbstractButton*>(o)->isPressed(); },
        [](Object*, const std::any&) { throw std::invalid_argument("pressed is read-only"); });
    setImplicitWidth(88);
    setImplicitHeight(36);
}
AbstractButton::~AbstractButton() = default;
String AbstractButton::text() const { return d->text; }
void AbstractButton::setText(String text) {
    auto sceneLock = lockScene();
    if (d->text == text) return;
    d->text = std::move(text);
    updateLabel();
    float w = 0, h = 0;
    labelSize(w, h);
    setImplicitWidth(std::max(72.0f, w + 44));
    setImplicitHeight(std::max(32.0f, h + 16));
    update();
    notifyPropertyChanged("text");
}
Font AbstractButton::font() const { return d->font; }
void AbstractButton::setFont(Font font) {
    auto sceneLock = lockScene();
    d->font = std::move(font);
    updateLabel();
    float w = 0, h = 0;
    labelSize(w, h);
    setImplicitWidth(std::max(72.0f, w + 44));
    setImplicitHeight(std::max(32.0f, h + 16));
    update();
    notifyPropertyChanged("font");
}
int AbstractButton::textScale() const { return std::max(1, d->font.effectivePixelSize() / 8); }
bool AbstractButton::isChecked() const { return d->checked; }
void AbstractButton::setChecked(bool checked) {
    auto sceneLock = lockScene();
    if (d->checked == checked) return;
    d->checked = checked;
    update();
    notifyPropertyChanged("checked");
    emitSignal(signalToggled, checked);
}
bool AbstractButton::checkable() const { return d->checkable; }
void AbstractButton::setCheckable(bool checkable) { auto lock = lockScene(); d->checkable = checkable; }
void AbstractButton::toggle() { if (d->checkable) setChecked(!d->checked); }
void AbstractButton::click() {
    auto sceneLock = lockScene(); toggle(); emitSignal(signalClicked); }
bool AbstractButton::isPressed() const { return d->pressed; }
void AbstractButton::setPressed(bool pressed) {
    auto sceneLock = lockScene();
    if (d->pressed == pressed) return;
    d->pressed = pressed;
    update();
    notifyPropertyChanged("pressed");
}
void AbstractButton::updateLabel() {
    // Native rasterization (default family: Inter); the built-in 8x8 bitmap
    // font remains the fallback when the font stack is unavailable.
    d->label = rasterizeText(d->text.toStdString(), d->font);
    ++d->labelGeneration;
}
void AbstractButton::labelSize(float& w, float& h) const {
    if (d->label.valid) {
        w = static_cast<float>(d->label.width);
        h = static_cast<float>(d->label.height);
        return;
    }
    Painter::textSize(d->text.toStdString(), textScale(), w, h);
}
void AbstractButton::drawLabel(Painter& painter, float x, float y, Color color) {
    if (d->label.valid) {
        painter.drawRgba(RectF{x, y, static_cast<float>(d->label.width),
                               static_cast<float>(d->label.height)},
                         d->label.rgba.data(), d->label.width, d->label.height,
                         this, d->labelGeneration, color);
        return;
    }
    painter.drawText(x, y, d->text.toStdString(), color, textScale());
}
void AbstractButton::paint(Painter& painter) {
    const Theme* theme = Theme::instance();
    const Color background = !isEnabled() ? Color::rgb(70, 74, 80)
        : d->pressed ? theme->highlightDimmerColor()
        : d->checked ? theme->highlightColor()
                    : theme->darkSecondaryColor().lighter(1.5);
    const float radius = static_cast<float>(theme->controlRadius());
    painter.fillRoundedRect(RectF{0, 0, width(), height()}, radius, background);
    painter.fillRect(RectF{radius, 0, width() - 2 * radius, 1},
                     theme->secondaryHighlightColor());
}
void AbstractButton::mouseEvent(MouseEvent& event) {
    if (event.action == MouseEvent::Action::Press && event.button == 1) {
        setPressed(true); event.accepted = true;
    } else if (event.action == MouseEvent::Action::Release && d->pressed) {
        setPressed(false); event.accepted = true;
        if (RectF{0, 0, width(), height()}.contains(event.pos.x, event.pos.y)) click();
    }
}
void AbstractButton::touchEvent(TouchEvent& event) {
    if (event.points.empty()) return;
    MouseEvent mouse;
    mouse.pos = event.points.front().pos; mouse.button = 1;
    mouse.action = event.action == TouchEvent::Action::Begin ? MouseEvent::Action::Press
        : event.action == TouchEvent::Action::End ? MouseEvent::Action::Release : MouseEvent::Action::Move;
    mouseEvent(mouse); event.accepted = mouse.accepted;
}
void AbstractButton::keyEvent(KeyEvent& event) {
    if (event.pressed && (event.key == 28 || event.key == 57)) {
        click(); event.accepted = true;
    }
}

Button::Button(Object* parent)
    : AbstractButton(parent), d(std::make_unique<Impl>()) {}
Button::~Button() = default;
bool Button::isFlat() const { return d->flat; }
void Button::setFlat(bool flat) { auto lock = lockScene(); d->flat = flat; update(); }
void Button::paint(Painter& painter) {
    if (!d->flat) AbstractButton::paint(painter);
    float w = 0, h = 0;
    labelSize(w, h);
    drawLabel(painter, (width() - w) / 2, (height() - h) / 2, Color::rgb(245, 247, 250));
}

CheckBox::CheckBox(Object* parent)
    : AbstractButton(parent), d(std::make_unique<Impl>()) {
    setCheckable(true);
    registerProperty<bool>("tristate", &CheckBox::tristate, &CheckBox::setTristate);
}
CheckBox::~CheckBox() = default;
CheckBox::CheckState CheckBox::checkState() const { return d->checkState; }
bool CheckBox::tristate() const { return d->tristate; }
void CheckBox::setCheckState(CheckState state) {
    auto sceneLock = lockScene();
    if (d->checkState == state) return;
    d->checkState = state;
    AbstractButton::setChecked(state == CheckState::Checked);
    update();
    notifyPropertyChanged("checkState");
}
void CheckBox::toggle() {
    if (d->tristate) {
        const CheckState next = d->checkState == CheckState::Unchecked ? CheckState::PartiallyChecked
            : d->checkState == CheckState::PartiallyChecked ? CheckState::Checked : CheckState::Unchecked;
        setCheckState(next);
    } else {
        setCheckState(isChecked() ? CheckState::Unchecked : CheckState::Checked);
    }
}
void CheckBox::paint(Painter& painter) {
    const Theme* theme = Theme::instance();
    const float side = std::min(24.0f, height() - 8);
    const float top = (height() - side) / 2;
    const float radius = static_cast<float>(theme->controlRadius()) * 0.6f;
    painter.fillRoundedRect(RectF{2, top, side, side}, radius,
                            theme->lightPrimaryColor());
    painter.fillRoundedRect(RectF{4, top + 2, side - 4, side - 4},
                            std::max(0.0f, radius - 2.0f),
                            theme->darkSecondaryColor());
    if (d->checkState != CheckState::Unchecked) {
        const float inset = d->checkState == CheckState::PartiallyChecked ? side / 3 : 5;
        painter.fillRoundedRect(RectF{2 + inset, top + inset, side - 2 * inset,
                                      side - 2 * inset},
                                std::max(0.0f, radius - inset + 2.0f),
                                theme->highlightColor());
    }
    float labelW = 0, labelH = 0;
    labelSize(labelW, labelH);
    drawLabel(painter, side + 10, (height() - labelH) / 2,
              theme->primaryColor());
}

RadioButton::RadioButton(Object* parent)
    : AbstractButton(parent), d(std::make_unique<Impl>()) {
    setCheckable(true);
}
RadioButton::~RadioButton() = default;
bool RadioButton::autoExclusive() const { return d->autoExclusive; }
void RadioButton::setAutoExclusive(bool enabled) { auto lock = lockScene(); d->autoExclusive = enabled; }
void RadioButton::toggle() {
    if (!d->autoExclusive) { setChecked(!isChecked()); return; }
    setChecked(true);
    if (parentItem()) {
        for (Item* sibling : parentItem()->childItems()) {
            auto* radio = dynamic_cast<RadioButton*>(sibling);
            if (radio && radio != this && radio->d->autoExclusive)
                radio->setChecked(false);
        }
    }
}
void RadioButton::paint(Painter& painter) {
    const Theme* theme = Theme::instance();
    const float side = std::min(22.0f, height() - 8);
    const float top = (height() - side) / 2;
    // Fully-rounded rects read as circles: outer ring, dark well, inner dot.
    painter.fillRoundedRect(RectF{3, top, side, side}, side / 2,
                            theme->lightPrimaryColor());
    painter.fillRoundedRect(RectF{5, top + 2, side - 4, side - 4},
                            (side - 4) / 2, theme->darkSecondaryColor());
    if (isChecked())
        painter.fillRoundedRect(RectF{9, top + 6, side - 12, side - 12},
                                (side - 12) / 2, theme->highlightColor());
    float labelW = 0, labelH = 0;
    labelSize(labelW, labelH);
    drawLabel(painter, side + 12, (height() - labelH) / 2,
              theme->primaryColor());
}

Switch::Switch(Object* parent) : AbstractButton(parent) {
    setCheckable(true);
    setImplicitWidth(64);
    setImplicitHeight(30);
}
void Switch::paint(Painter& painter) {
    const Theme* theme = Theme::instance();
    const float trackWidth = std::min(52.0f, width() - 4);
    const float trackHeight = std::min(22.0f, height() - 4);
    const float top = (height() - trackHeight) / 2;
    // Pill-shaped track, round knob.
    painter.fillRoundedRect(RectF{2, top, trackWidth, trackHeight},
                            trackHeight / 2,
                            isChecked() ? theme->highlightColor()
                                        : Color::rgb(85, 92, 102));
    const float knob = trackHeight - 4;
    const float x = isChecked() ? 2 + trackWidth - knob - 2 : 4;
    painter.fillRoundedRect(RectF{x, top + 2, knob, knob}, knob / 2,
                            theme->lightPrimaryColor());
    if (!text().isEmpty()) {
        float labelW = 0, labelH = 0;
        labelSize(labelW, labelH);
        drawLabel(painter, trackWidth + 10, (height() - labelH) / 2,
                  theme->primaryColor());
    }
}
} // namespace Controls
} // namespace Gui
} // namespace Firmir

void Firmir::Gui::Controls::CheckBox::setTristate(bool enabled) {
    auto lock = lockScene();
    if (d->tristate == enabled) return;
    d->tristate = enabled;
    update();
    notifyPropertyChanged("tristate");
}
