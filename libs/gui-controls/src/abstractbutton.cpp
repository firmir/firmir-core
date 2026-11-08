#include <firmir/gui-controls/abstractbutton.hpp>
#include <algorithm>

namespace Firmir {
namespace Gui {
namespace Controls {

AbstractButton::AbstractButton(Object* parent) : Item(parent) {
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
void AbstractButton::setText(String text) {
    auto sceneLock = lockScene();
    if (m_text == text) return;
    m_text = std::move(text);
    updateLabel();
    float w = 0, h = 0;
    labelSize(w, h);
    setImplicitWidth(std::max(72.0f, w + 44));
    setImplicitHeight(std::max(32.0f, h + 16));
    update();
    notifyPropertyChanged("text");
}
void AbstractButton::setFont(Font font) {
    auto sceneLock = lockScene();
    m_font = std::move(font);
    updateLabel();
    float w = 0, h = 0;
    labelSize(w, h);
    setImplicitWidth(std::max(72.0f, w + 44));
    setImplicitHeight(std::max(32.0f, h + 16));
    update();
    notifyPropertyChanged("font");
}
int AbstractButton::textScale() const { return std::max(1, m_font.effectivePixelSize() / 8); }
void AbstractButton::setChecked(bool checked) {
    auto sceneLock = lockScene();
    if (m_checked == checked) return;
    m_checked = checked;
    update();
    notifyPropertyChanged("checked");
    emitSignal(signalToggled, checked);
}
void AbstractButton::toggle() { if (m_checkable) setChecked(!m_checked); }
void AbstractButton::click() {
    auto sceneLock = lockScene(); toggle(); emitSignal(signalClicked); }
void AbstractButton::setPressed(bool pressed) {
    auto sceneLock = lockScene();
    if (m_pressed == pressed) return;
    m_pressed = pressed;
    update();
    notifyPropertyChanged("pressed");
}
void AbstractButton::updateLabel() {
    // Native rasterization (default family: Inter); the built-in 8x8 bitmap
    // font remains the fallback when the font stack is unavailable.
    m_label = rasterizeText(m_text.toStdString(), m_font);
    ++m_labelGeneration;
}
void AbstractButton::labelSize(float& w, float& h) const {
    if (m_label.valid) {
        w = static_cast<float>(m_label.width);
        h = static_cast<float>(m_label.height);
        return;
    }
    Painter::textSize(m_text.toStdString(), textScale(), w, h);
}
void AbstractButton::drawLabel(Painter& painter, float x, float y, Color color) {
    if (m_label.valid) {
        painter.drawRgba(RectF{x, y, static_cast<float>(m_label.width),
                               static_cast<float>(m_label.height)},
                         m_label.rgba.data(), m_label.width, m_label.height,
                         this, m_labelGeneration, color);
        return;
    }
    painter.drawText(x, y, m_text.toStdString(), color, textScale());
}
void AbstractButton::paint(Painter& painter) {
    const Color background = !isEnabled() ? Color::rgb(70, 74, 80)
        : m_pressed ? Color::rgb(40, 90, 145) : m_checked ? Color::rgb(45, 120, 180)
                                                         : Color::rgb(55, 65, 78);
    painter.fillRect(RectF{0, 0, width(), height()}, background);
    painter.fillRect(RectF{0, 0, width(), 1}, Color::rgb(110, 130, 150));
}
void AbstractButton::mouseEvent(MouseEvent& event) {
    if (event.action == MouseEvent::Action::Press && event.button == 1) {
        setPressed(true); event.accepted = true;
    } else if (event.action == MouseEvent::Action::Release && m_pressed) {
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

Button::Button(Object* parent) : AbstractButton(parent) {}
void Button::paint(Painter& painter) {
    if (!m_flat) AbstractButton::paint(painter);
    float w = 0, h = 0;
    labelSize(w, h);
    drawLabel(painter, (width() - w) / 2, (height() - h) / 2, Color::rgb(245, 247, 250));
}

CheckBox::CheckBox(Object* parent) : AbstractButton(parent) {
    setCheckable(true);
    registerProperty<bool>("tristate", &CheckBox::tristate, &CheckBox::setTristate);
}
void CheckBox::setCheckState(CheckState state) {
    auto sceneLock = lockScene();
    if (m_checkState == state) return;
    m_checkState = state;
    AbstractButton::setChecked(state == CheckState::Checked);
    update();
    notifyPropertyChanged("checkState");
}
void CheckBox::toggle() {
    if (m_tristate) {
        const CheckState next = m_checkState == CheckState::Unchecked ? CheckState::PartiallyChecked
            : m_checkState == CheckState::PartiallyChecked ? CheckState::Checked : CheckState::Unchecked;
        setCheckState(next);
    } else {
        setCheckState(isChecked() ? CheckState::Unchecked : CheckState::Checked);
    }
}
void CheckBox::paint(Painter& painter) {
    const float side = std::min(24.0f, height() - 8);
    const float top = (height() - side) / 2;
    painter.fillRect(RectF{2, top, side, side}, Color::rgb(235, 238, 242));
    painter.fillRect(RectF{4, top + 2, side - 4, side - 4}, Color::rgb(45, 55, 68));
    if (m_checkState != CheckState::Unchecked) {
        const float inset = m_checkState == CheckState::PartiallyChecked ? side / 3 : 5;
        painter.fillRect(RectF{2 + inset, top + inset, side - 2 * inset, side - 2 * inset},
                         Color::rgb(70, 170, 240));
    }
    float labelW = 0, labelH = 0;
    labelSize(labelW, labelH);
    drawLabel(painter, side + 10, (height() - labelH) / 2, Color::rgb(235, 238, 242));
}

RadioButton::RadioButton(Object* parent) : AbstractButton(parent) { setCheckable(true); }
void RadioButton::toggle() {
    if (!m_autoExclusive) { setChecked(!isChecked()); return; }
    setChecked(true);
    if (parentItem()) {
        for (Item* sibling : parentItem()->childItems()) {
            auto* radio = dynamic_cast<RadioButton*>(sibling);
            if (radio && radio != this && radio->m_autoExclusive)
                radio->setChecked(false);
        }
    }
}
void RadioButton::paint(Painter& painter) {
    const float side = std::min(22.0f, height() - 8);
    const float top = (height() - side) / 2;
    // Concentric squares keep the primitive renderer backend-independent.
    painter.fillRect(RectF{3, top, side, side}, Color::rgb(225, 230, 236));
    painter.fillRect(RectF{5, top + 2, side - 4, side - 4}, Color::rgb(45, 55, 68));
    if (isChecked())
        painter.fillRect(RectF{9, top + 6, side - 12, side - 12}, Color::rgb(70, 170, 240));
    float labelW = 0, labelH = 0;
    labelSize(labelW, labelH);
    drawLabel(painter, side + 12, (height() - labelH) / 2, Color::rgb(235, 238, 242));
}

Switch::Switch(Object* parent) : AbstractButton(parent) {
    setCheckable(true);
    setImplicitWidth(64);
    setImplicitHeight(30);
}
void Switch::paint(Painter& painter) {
    const float trackWidth = std::min(52.0f, width() - 4);
    const float trackHeight = std::min(22.0f, height() - 4);
    const float top = (height() - trackHeight) / 2;
    painter.fillRect(RectF{2, top, trackWidth, trackHeight},
                     isChecked() ? Color::rgb(45, 145, 205) : Color::rgb(85, 92, 102));
    const float knob = trackHeight - 4;
    const float x = isChecked() ? 2 + trackWidth - knob - 2 : 4;
    painter.fillRect(RectF{x, top + 2, knob, knob}, Color::rgb(245, 247, 250));
    if (!text().isEmpty()) {
        float labelW = 0, labelH = 0;
        labelSize(labelW, labelH);
        drawLabel(painter, trackWidth + 10, (height() - labelH) / 2,
                  Color::rgb(235, 238, 242));
    }
}
} // namespace Controls
} // namespace Gui
} // namespace Firmir

void Firmir::Gui::Controls::CheckBox::setTristate(bool enabled) {
    auto lock = lockScene();
    if (m_tristate == enabled) return;
    m_tristate = enabled;
    update();
    notifyPropertyChanged("tristate");
}
