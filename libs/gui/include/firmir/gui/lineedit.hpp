#pragma once

// Firmir::Gui::LineEdit — minimal single/multi-line text input item:
// click to focus, type to edit, Enter emits "accepted" (single-line) or
// inserts a newline (multiline), Backspace deletes the last UTF-8 code
// point. Exposes the "text" and "multiline" properties (Firml-usable).

#include <firmir/gui/item.hpp>
#include <firmir/gui/rectangle.hpp>
#include <firmir/gui/text.hpp>
#include <firmir/string.hpp>

namespace Firmir {
namespace Gui {

class LineEdit : public Item {
public:
    explicit LineEdit(Object* parent = nullptr);
    ~LineEdit() override;

    String text() const { return m_value; }
    void setText(String text);
    bool multiline() const { return m_multiline; }
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

    Text m_text;
    Rectangle m_cursor;
    String m_value;
    bool m_multiline = false;
};

} // namespace Gui
} // namespace Firmir
