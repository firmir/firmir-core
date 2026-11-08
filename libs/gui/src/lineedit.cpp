#include <firmir/gui/lineedit.hpp>

#include <firmir/gui/painter.hpp>

namespace Firmir {
namespace Gui {

LineEdit::LineEdit(Object* parent)
    : Item(parent), m_text(this), m_cursor(this) {
    setClip(true);
    setFocusPolicy(FocusPolicy::ClickFocus);
    m_text.setX(10);
    m_text.setY(7);
    m_text.setColor(Color::rgb(0xE8, 0xE8, 0xE8));
    m_cursor.setWidth(2);
    m_cursor.setHeight(22);
    m_cursor.setY(6);
    m_cursor.setColor(Color::rgb(0x4A, 0x90, 0xD9));
    m_cursor.setVisible(false);
    registerProperty<String>("text", &LineEdit::text, &LineEdit::setText);
    registerProperty<bool>("multiline", &LineEdit::multiline,
                           &LineEdit::setMultiline);
}

LineEdit::~LineEdit() = default;

void LineEdit::setText(String text) {
    if (m_value == text)
        return;
    m_value = std::move(text);
    m_text.setText(m_value);
    updateCursor();
    update();
    notifyPropertyChanged("text");
}

void LineEdit::setMultiline(bool multiline) {
    if (m_multiline == multiline)
        return;
    m_multiline = multiline;
    updateCursor();
    notifyPropertyChanged("multiline");
}

void LineEdit::keyEvent(KeyEvent& event) {
    if (!event.pressed)
        return;
    if (event.key == 14) { // KEY_BACKSPACE
        std::string bytes = m_value.toStdString();
        while (!bytes.empty() &&
               (static_cast<unsigned char>(bytes.back()) & 0xC0) == 0x80)
            bytes.pop_back();
        if (!bytes.empty())
            bytes.pop_back();
        setText(String(bytes.c_str()));
    } else if (event.key == 28 && !m_multiline) { // KEY_ENTER
        emitSignal(signalAccepted);
    } else if (event.key == 28 && m_multiline) {
        setText(m_value + String("\n"));
    } else if (!event.text.empty()) {
        setText(m_value + String(event.text.c_str()));
    }
    event.accepted = true;
}

void LineEdit::focusInEvent() {
    updateCursor();
    update();
}

void LineEdit::focusOutEvent() {
    m_cursor.setVisible(false);
    update();
}

void LineEdit::paint(Painter& painter) {
    painter.fillRect(RectF{0, 0, width(), height()},
                     hasFocus() ? Color::rgb(0x3A, 0x3F, 0x49)
                                : Color::rgb(0x2E, 0x31, 0x38));
    if (hasFocus())
        painter.fillRect(RectF{0, height() - 2, width(), 2},
                         Color::rgb(0x4A, 0x90, 0xD9));
}

void LineEdit::updateCursor() {
    float tw = 0, th = 0;
    Painter::textSize(m_value.toStdString(), 2, tw, th);
    m_cursor.setX(10 + tw + 2);
    m_cursor.setVisible(hasFocus() && !m_multiline);
}

} // namespace Gui
} // namespace Firmir
