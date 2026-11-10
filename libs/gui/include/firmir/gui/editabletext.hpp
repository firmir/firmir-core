#pragma once

// Firmir::Gui::EditableText — minimal single/multi-line text input item:
// click to focus, type to edit, Enter emits "accepted" (single-line) or
// inserts a newline (multiline), Backspace/Delete delete the last UTF-8
// code point. Exposes the "text" and "multiline" properties (Firml-usable).
//
// (Renamed from LineEdit: the item edits arbitrary text, not just lines.)

#include <firmir/gui/item.hpp>
#include <firmir/string.hpp>

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

    /// Emitted on Enter in single-line mode (no arguments).
    static constexpr auto signalAccepted = "accepted";

protected:
    void keyEvent(KeyEvent& event) override;
    void focusInEvent() override;
    void focusOutEvent() override;
    void paint(Painter& painter) override;

private:
    void updateCursor();
    void deleteLastCodePoint();

    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
