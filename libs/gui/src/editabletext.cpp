#include <firmir/gui/editabletext.hpp>

#include <firmir/gui/painter.hpp>
#include <firmir/gui/rectangle.hpp>
#include <firmir/gui/text.hpp>
#include <firmir/gui/theme.hpp>

#include <algorithm>
#include <cstdio>

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
/// deliver them as text (xkb BackSpace -> "\x08").
bool isBackspaceText(const std::string& text) {
    return text == "\x08";
}

/// True for the control text meaning "delete forwards" (xkb DEL -> "\x7F").
bool isDeleteText(const std::string& text) {
    return text == "\x7F";
}

bool isContinuationByte(char c) {
    return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

/// Previous UTF-8 code point boundary strictly before @p pos.
std::size_t prevBoundary(const std::string& s, std::size_t pos) {
    if (pos > s.size())
        pos = s.size();
    if (pos == 0)
        return 0;
    --pos; // last byte of the previous code point
    while (pos > 0 && isContinuationByte(s[pos]))
        --pos; // land on its lead byte
    return pos;
}

/// Next UTF-8 code point boundary strictly after @p pos.
std::size_t nextBoundary(const std::string& s, std::size_t pos) {
    if (pos >= s.size())
        return s.size();
    ++pos;
    while (pos < s.size() && isContinuationByte(s[pos]))
        ++pos;
    return pos;
}

/// Number of code point boundaries in [0, bytePos) — i.e. the ordinal of the
/// caret position at @p bytePos.
int boundaryOrdinal(const std::string& s, std::size_t bytePos) {
    int ordinal = 0;
    for (std::size_t i = 0; i < bytePos && i < s.size(); ++i)
        if (!isContinuationByte(s[i]))
            ++ordinal;
    return ordinal;
}

/// Byte offset of the code point boundary with the given ordinal (clamped).
std::size_t byteAtOrdinal(const std::string& s, int ordinal) {
    if (ordinal <= 0)
        return 0;
    int seen = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (isContinuationByte(s[i]))
            continue;
        if (seen == ordinal)
            return i;
        ++seen;
    }
    return s.size();
}

// evdev key codes (linux/input-event-codes.h) — same raw-number convention
// as the rest of the item.
constexpr int kKeyEscape = 1;
constexpr int kKeyBackspace = 14;
constexpr int kKeyEnter = 28;
constexpr int kKeyLeftCtrl = 29;
constexpr int kKeyRightCtrl = 97;
constexpr int kKeyA = 30;
constexpr int kKeyLeftShift = 42;
constexpr int kKeyRightShift = 54;
constexpr int kKeyHome = 102;
constexpr int kKeyLeft = 105;
constexpr int kKeyRight = 106;
constexpr int kKeyEnd = 107;
constexpr int kKeyDelete = 111;

/// Left padding of the text inside the edit; the rasterized bitmap itself
/// has a 1px left pad (see rasterizeText), hence +1 for caret/highlight
/// positions so they line up with the rendered glyphs.
constexpr float kTextPadding = 10.0f;
constexpr float kBitmapPad = 1.0f;

} // namespace

class EditableText::Impl {
public:
    explicit Impl(EditableText* q) : text(q), cursor(q) {}

    Text text;
    Rectangle cursor;
    String value;
    bool multiline = false;
    std::size_t cursorByte = 0; ///< caret byte offset (code point boundary)
    std::size_t anchorByte = 0; ///< selection anchor; selection when != cursor
    bool dragging = false;      ///< mouse drag selection in progress
    bool shiftHeld = false;     ///< tracked from Shift key events
    bool ctrlHeld = false;
};

EditableText::EditableText(Object* parent)
    : Item(parent), d(std::make_unique<Impl>(this)) {
    setClip(true);
    setFocusPolicy(FocusPolicy::ClickFocus);
    // Decoration children must not take clicks/keys of their own: the window
    // focuses the nearest click-focusable ancestor (this edit).
    d->text.setFocusPolicy(FocusPolicy::NoFocus);
    d->cursor.setFocusPolicy(FocusPolicy::NoFocus);
    d->text.setX(10);
    d->text.setY(7);
    d->text.setColor(Theme::instance()->primaryColor());
    d->cursor.setWidth(2);
    d->cursor.setHeight(22);
    d->cursor.setY(6);
    d->cursor.setColor(Theme::instance()->highlightColor());
    d->cursor.setVisible(false);
    registerProperty<String>("text", &EditableText::text,
                             &EditableText::setText);
    registerProperty<bool>("multiline", &EditableText::multiline,
                           &EditableText::setMultiline);
    registerProperty<int>("cursorPosition", &EditableText::cursorPosition,
                          &EditableText::setCursorPosition);
    registerPropertyImpl(
        "selectionStart", typeid(int),
        [](const Object* object) -> std::any {
            return static_cast<const EditableText*>(object)->selectionStart();
        },
        [](Object*, const std::any&) {
            throw std::invalid_argument("selectionStart is read-only");
        });
    registerPropertyImpl(
        "selectionEnd", typeid(int),
        [](const Object* object) -> std::any {
            return static_cast<const EditableText*>(object)->selectionEnd();
        },
        [](Object*, const std::any&) {
            throw std::invalid_argument("selectionEnd is read-only");
        });
    registerPropertyImpl(
        "selectedText", typeid(String),
        [](const Object* object) -> std::any {
            return static_cast<const EditableText*>(object)->selectedText();
        },
        [](Object*, const std::any&) {
            throw std::invalid_argument("selectedText is read-only");
        });
}

EditableText::~EditableText() = default;

String EditableText::text() const { return d->value; }
bool EditableText::multiline() const { return d->multiline; }

int EditableText::cursorPosition() const {
    return boundaryOrdinal(d->value.toStdString(), d->cursorByte);
}

void EditableText::setCursorPosition(int position) {
    const std::string bytes = d->value.toStdString();
    const int count = boundaryOrdinal(bytes, bytes.size());
    setCaret(byteAtOrdinal(bytes, std::clamp(position, 0, count)), false);
}

int EditableText::selectionStart() const {
    return boundaryOrdinal(d->value.toStdString(), selectionBounds().first);
}

int EditableText::selectionEnd() const {
    return boundaryOrdinal(d->value.toStdString(), selectionBounds().second);
}

String EditableText::selectedText() const {
    const std::string bytes = d->value.toStdString();
    const auto [lo, hi] = selectionBounds();
    return String(bytes.substr(lo, hi - lo));
}

void EditableText::clearSelection() {
    if (!hasSelection())
        return;
    d->anchorByte = d->cursorByte;
    update();
    notifyPropertyChanged("selectionStart");
    notifyPropertyChanged("selectionEnd");
    notifyPropertyChanged("selectedText");
}

void EditableText::applyValue(std::string bytes) {
    // No early-out on equal text: callers already gate on actual changes.
    d->value = String(bytes);
    d->text.setText(d->value);
    updateCursor();
    update();
    notifyPropertyChanged("text");
}

void EditableText::setText(String text) {
    if (d->value == text)
        return;
    applyValue(text.toStdString());
    // Programmatic edits place the caret at the end and drop the selection.
    d->cursorByte = d->anchorByte = d->value.toStdString().size();
    updateCursor();
    update();
    notifyPropertyChanged("cursorPosition");
    notifyPropertyChanged("selectionStart");
    notifyPropertyChanged("selectionEnd");
    notifyPropertyChanged("selectedText");
}

void EditableText::setMultiline(bool multiline) {
    if (d->multiline == multiline)
        return;
    d->multiline = multiline;
    if (multiline)
        clearSelection(); // selection is a single-line feature
    updateCursor();
    notifyPropertyChanged("multiline");
}

bool EditableText::hasSelection() const {
    return d->cursorByte != d->anchorByte;
}

std::pair<std::size_t, std::size_t> EditableText::selectionBounds() const {
    return {std::min(d->cursorByte, d->anchorByte),
            std::max(d->cursorByte, d->anchorByte)};
}

void EditableText::setCaret(std::size_t bytePos, bool extend) {
    const std::string bytes = d->value.toStdString();
    bytePos = std::min(bytePos, bytes.size());
    // Never park the caret inside a code point.
    while (bytePos > 0 && bytePos < bytes.size() &&
           isContinuationByte(bytes[bytePos]))
        --bytePos;
    const bool hadSelection = hasSelection();
    const std::size_t oldCaret = d->cursorByte;
    d->cursorByte = bytePos;
    if (!extend)
        d->anchorByte = bytePos;
    if (oldCaret == d->cursorByte && !extend)
        return;
    updateCursor();
    update();
    if (oldCaret != d->cursorByte)
        notifyPropertyChanged("cursorPosition");
    if (hasSelection() || hadSelection) {
        notifyPropertyChanged("selectionStart");
        notifyPropertyChanged("selectionEnd");
        notifyPropertyChanged("selectedText");
    }
}

void EditableText::selectAll() {
    const std::string bytes = d->value.toStdString();
    d->anchorByte = 0;
    setCaret(bytes.size(), true);
}

void EditableText::deleteSelection() {
    if (!hasSelection())
        return;
    std::string bytes = d->value.toStdString();
    const auto [lo, hi] = selectionBounds();
    bytes.erase(lo, hi - lo);
    applyValue(bytes);
    setCaret(lo, false);
}

void EditableText::backspace() {
    if (hasSelection()) {
        deleteSelection();
        return;
    }
    if (d->cursorByte == 0)
        return;
    std::string bytes = d->value.toStdString();
    const std::size_t prev = prevBoundary(bytes, d->cursorByte);
    bytes.erase(prev, d->cursorByte - prev);
    applyValue(bytes);
    setCaret(prev, false);
}

void EditableText::forwardDelete() {
    if (hasSelection()) {
        deleteSelection();
        return;
    }
    std::string bytes = d->value.toStdString();
    if (d->cursorByte >= bytes.size())
        return;
    bytes.erase(d->cursorByte,
                nextBoundary(bytes, d->cursorByte) - d->cursorByte);
    applyValue(bytes);
    setCaret(d->cursorByte, false); // same position; reclamps + repaints
}

void EditableText::insertText(const std::string& utf8) {
    if (hasSelection())
        deleteSelection();
    std::string bytes = d->value.toStdString();
    bytes.insert(d->cursorByte, utf8);
    applyValue(bytes);
    setCaret(d->cursorByte + utf8.size(), false);
}

void EditableText::keyEvent(KeyEvent& event) {
    // Shift/Ctrl state is tracked here: KeyEvent carries no modifier flags.
    if (event.key == kKeyLeftShift || event.key == kKeyRightShift) {
        d->shiftHeld = event.pressed;
        event.accepted = true;
        return;
    }
    if (event.key == kKeyLeftCtrl || event.key == kKeyRightCtrl) {
        d->ctrlHeld = event.pressed;
        event.accepted = true;
        return;
    }
    if (!event.pressed)
        return;

    if (d->multiline) {
        // Append-only editing; no caret movement/selection here.
        if (event.key == kKeyBackspace || event.key == kKeyDelete ||
            isBackspaceText(event.text) || isDeleteText(event.text)) {
            std::string bytes = d->value.toStdString();
            const std::size_t prev = prevBoundary(bytes, bytes.size());
            if (prev < bytes.size())
                setText(String(bytes.substr(0, prev)));
        } else if (event.key == kKeyEnter) {
            setText(d->value + String("\n"));
        } else if (isPrintableText(event.text)) {
            setText(d->value + String(event.text.c_str()));
        }
        event.accepted = true;
        return;
    }

    if (event.key == kKeyLeft) {
        // Plain Left collapses an active selection to its start.
        if (hasSelection() && !d->shiftHeld)
            setCaret(selectionBounds().first, false);
        else
            setCaret(prevBoundary(d->value.toStdString(), d->cursorByte),
                     d->shiftHeld);
    } else if (event.key == kKeyRight) {
        if (hasSelection() && !d->shiftHeld)
            setCaret(selectionBounds().second, false);
        else
            setCaret(nextBoundary(d->value.toStdString(), d->cursorByte),
                     d->shiftHeld);
    } else if (event.key == kKeyHome) {
        setCaret(0, d->shiftHeld);
    } else if (event.key == kKeyEnd) {
        setCaret(d->value.toStdString().size(), d->shiftHeld);
    } else if (event.key == kKeyA && d->ctrlHeld) {
        selectAll();
    } else if (event.key == kKeyEscape) {
        clearSelection();
    } else if (event.key == kKeyBackspace || isBackspaceText(event.text)) {
        backspace();
    } else if (event.key == kKeyDelete || isDeleteText(event.text)) {
        forwardDelete();
    } else if (event.key == kKeyEnter) {
        emitSignal(signalAccepted);
    } else if (isPrintableText(event.text)) {
        insertText(event.text);
    }
    event.accepted = true;
}

void EditableText::focusInEvent() {
    updateCursor();
    update();
}

void EditableText::mouseEvent(MouseEvent& event) {
    // Single-line only: multiline keeps the plain click-to-focus behavior.
    if (d->multiline)
        return;
    switch (event.action) {
    case MouseEvent::Action::Press:
        if (event.button != 1)
            return;
        // Shift+click extends from the existing anchor; a plain click
        // places the caret and collapses any selection.
        setCaret(hitTest(event.pos.x), d->shiftHeld);
        d->dragging = true;
        event.accepted = true; // become the mouse grabber for the drag
        break;
    case MouseEvent::Action::Move:
        if (!d->dragging)
            return;
        setCaret(hitTest(event.pos.x), true);
        event.accepted = true;
        break;
    case MouseEvent::Action::Release:
        if (event.button != 1 || !d->dragging)
            return;
        d->dragging = false;
        event.accepted = true;
        break;
    default:
        break;
    }
}

std::size_t EditableText::hitTest(float x) const {
    const std::string bytes = d->value.toStdString();
    const std::vector<float> positions = caretPositions(bytes, d->text.font());
    const float local = x - kTextPadding - kBitmapPad;
    int ordinal = static_cast<int>(positions.size()) - 1;
    for (std::size_t i = 0; i + 1 < positions.size(); ++i) {
        if (local < (positions[i] + positions[i + 1]) * 0.5f) {
            ordinal = static_cast<int>(i);
            break;
        }
    }
    return byteAtOrdinal(bytes, ordinal);
}

void EditableText::focusOutEvent() {
    d->cursor.setVisible(false);
    d->dragging = false;
    clearSelection();
    update();
}

void EditableText::paint(Painter& painter) {
    const Theme* theme = Theme::instance();
    const float radius = static_cast<float>(theme->controlRadius()) * 0.6f;
    painter.fillRoundedRect(RectF{0, 0, width(), height()}, radius,
                            hasFocus() ? theme->darkSecondaryColor().lighter(1.25)
                                       : theme->darkSecondaryColor());
    // Selection highlight (painted here so the Text child draws on top).
    if (hasSelection() && hasFocus() && !d->multiline) {
        const std::string bytes = d->value.toStdString();
        const std::vector<float> positions =
            caretPositions(bytes, d->text.font());
        const int lo = boundaryOrdinal(bytes, selectionBounds().first);
        const int hi = boundaryOrdinal(bytes, selectionBounds().second);
        const float x0 = kTextPadding + kBitmapPad + positions[lo];
        const float x1 = kTextPadding + kBitmapPad + positions[hi];
        const Color highlight = theme->highlightColor();
        painter.fillRect(
            RectF{x0, d->cursor.y(), x1 - x0, d->cursor.height()},
            Color::rgba(highlight.r, highlight.g, highlight.b, 110));
    }
    if (hasFocus())
        painter.fillRect(RectF{radius, height() - 2, width() - 2 * radius, 2},
                         theme->highlightColor());
}

void EditableText::updateCursor() {
    // Caret x comes from the real shaped advances (caretPositions), not the
    // bitmap-font approximation of Painter::textSize.
    const std::string bytes = d->value.toStdString();
    const std::vector<float> positions = caretPositions(bytes, d->text.font());
    // caretPositions() describes the first line only, while the ordinal counts
    // boundaries over the whole text: after a newline it would index past the
    // end (the caret is hidden in multiline mode anyway, see below).
    const int ordinal = std::clamp(boundaryOrdinal(bytes, d->cursorByte), 0,
                                   static_cast<int>(positions.size()) - 1);
    d->cursor.setX(kTextPadding + kBitmapPad +
                   positions[static_cast<std::size_t>(ordinal)]);
    d->cursor.setVisible(hasFocus() && !d->multiline);
}

} // namespace Gui
} // namespace Firmir
