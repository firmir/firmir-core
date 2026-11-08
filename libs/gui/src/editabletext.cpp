#include <firmir/gui/editabletext.hpp>

#include <firmir/gui/painter.hpp>
#include <firmir/gui/theme.hpp>

namespace Firmir {
namespace Gui {

namespace {

/// True when the event text is printable input (not a control character some
/// backends attach to Backspace/Enter/Tab/Escape: BS, HT, CR/LF, DEL, ...).
/// Multi-byte UTF-8 sequences are always printable content.
bool isPrintableText(const std::string& text) {
    if (text.empty())
        return false;
    const unsigned char first = static_cast<unsigned char>(text.front());
    return first >= 0x20 && first != 0x7F;
}

/// True for the control texts that mean "delete backwards" on backends that
/// deliver them as text (xkb BackSpace -> "\x08", DEL -> "\x7F").
bool isBackspaceText(const std::string& text) {
    return text == "\x08" || text == "\x7F";
}

} // namespace

EditableText::EditableText(Object* parent)
    : Item(parent), m_text(this), m_cursor(this) {
    setClip(true);
    setFocusPolicy(FocusPolicy::ClickFocus);
    // Decoration children must not take clicks/keys of their own: the window
    // focuses the nearest click-focusable ancestor (this edit).
    m_text.setFocusPolicy(FocusPolicy::NoFocus);
    m_cursor.setFocusPolicy(FocusPolicy::NoFocus);
    m_text.setX(10);
    m_text.setY(7);
    m_text.setColor(Theme::instance()->primaryColor());
    m_cursor.setWidth(2);
    m_cursor.setHeight(22);
    m_cursor.setY(6);
    m_cursor.setColor(Theme::instance()->highlightColor());
    m_cursor.setVisible(false);
    registerProperty<String>("text", &EditableText::text,
                             &EditableText::setText);
    registerProperty<bool>("multiline", &EditableText::multiline,
                           &EditableText::setMultiline);
}

EditableText::~EditableText() = default;

void EditableText::setText(String text) {
    if (m_value == text)
        return;
    m_value = std::move(text);
    m_text.setText(m_value);
    updateCursor();
    update();
    notifyPropertyChanged("text");
}

void EditableText::setMultiline(bool multiline) {
    if (m_multiline == multiline)
        return;
    m_multiline = multiline;
    updateCursor();
    notifyPropertyChanged("multiline");
}

void EditableText::keyEvent(KeyEvent& event) {
    if (!event.pressed)
        return;
    if (event.key == 14 || event.key == 111 || // KEY_BACKSPACE / KEY_DELETE
        isBackspaceText(event.text)) {
        deleteLastCodePoint();
    } else if (event.key == 28 && !m_multiline) { // KEY_ENTER
        emitSignal(signalAccepted);
    } else if (event.key == 28 && m_multiline) {
        setText(m_value + String("\n"));
    } else if (isPrintableText(event.text)) {
        setText(m_value + String(event.text.c_str()));
    }
    event.accepted = true;
}

void EditableText::deleteLastCodePoint() {
    std::string bytes = m_value.toStdString();
    while (!bytes.empty() &&
           (static_cast<unsigned char>(bytes.back()) & 0xC0) == 0x80)
        bytes.pop_back();
    if (!bytes.empty())
        bytes.pop_back();
    setText(String(bytes.c_str()));
}

void EditableText::focusInEvent() {
    updateCursor();
    update();
}

void EditableText::focusOutEvent() {
    m_cursor.setVisible(false);
    update();
}

void EditableText::paint(Painter& painter) {
    const Theme* theme = Theme::instance();
    const float radius = static_cast<float>(theme->controlRadius()) * 0.6f;
    painter.fillRoundedRect(RectF{0, 0, width(), height()}, radius,
                            hasFocus() ? theme->darkSecondaryColor().lighter(1.25)
                                       : theme->darkSecondaryColor());
    if (hasFocus())
        painter.fillRect(RectF{radius, height() - 2, width() - 2 * radius, 2},
                         theme->highlightColor());
}

void EditableText::updateCursor() {
    // The real rendered width of the text (the bitmap-font approximation
    // Painter::textSize would put the cursor ~2x past the visible text,
    // looking like trailing spaces).
    m_cursor.setX(10 + m_text.implicitWidth() + 2);
    m_cursor.setVisible(hasFocus() && !m_multiline);
}

} // namespace Gui
} // namespace Firmir
