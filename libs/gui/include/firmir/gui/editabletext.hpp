#pragma once

// Firmir::Gui::EditableText — minimal single/multi-line text input item:
// click to focus, type to edit, Enter emits "accepted" (single-line) or
// inserts a newline (multiline). Single-line mode has a movable caret and
// text selection: Left/Right/Home/End move (Shift extends the selection,
// Ctrl+A selects all, Escape clears it), click places the caret, drag
// selects, Backspace/Delete remove the selection at once and typing
// replaces it. Exposes the "text", "multiline", "cursorPosition",
// "selectionStart", "selectionEnd" and "selectedText" properties
// (Firml-usable). Multiline mode keeps the simple append-only editing
// (no caret movement or selection).
//
// (Renamed from LineEdit: the item edits arbitrary text, not just lines.)

#include <firmir/gui/item.hpp>
#include <firmir/string.hpp>

#include <cstddef>
#include <memory>

namespace Firmir {
namespace Gui {

class EditableText : public Item {
public:
    explicit EditableText(Object* parent = nullptr);
    ~EditableText() override;

    String text() const;
    void setText(String text);
    bool multiline() const;
    void setMultiline(bool multiline);

    /// Caret position in code points (single-line mode; always at the text
    /// end in multiline mode). Setting it clears the selection.
    int cursorPosition() const;
    void setCursorPosition(int position);
    /// Selection range in code points; both equal cursorPosition() when
    /// nothing is selected.
    int selectionStart() const;
    int selectionEnd() const;
    String selectedText() const;
    void clearSelection();

    /// Emitted on Enter in single-line mode (no arguments).
    static constexpr auto signalAccepted = "accepted";

protected:
    void keyEvent(KeyEvent& event) override;
    void mouseEvent(MouseEvent& event) override;
    void focusInEvent() override;
    void focusOutEvent() override;
    void paint(Painter& painter) override;

private:
    bool hasSelection() const;
    /// [start, end) byte offsets of the selection; empty when none.
    std::pair<std::size_t, std::size_t> selectionBounds() const;
    void deleteSelection();
    void backspace();
    void forwardDelete();
    /// Inserts at the caret, replacing the selection when one is active.
    void insertText(const std::string& utf8);
    /// Moves the caret; with @p extend the selection anchor stays put.
    void setCaret(std::size_t bytePos, bool extend);
    void selectAll();
    /// Byte offset of the caret position closest to local x.
    std::size_t hitTest(float x) const;
    void updateCursor();
    void applyValue(std::string bytes); // text update without caret reset

    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
