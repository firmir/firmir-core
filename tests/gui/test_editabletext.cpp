// EditableText key handling: Enter/accepted vs multiline newline, Delete,
// Backspace on empty text, control-character text filtering. Uses the
// headless plugin's key injection like gui_editabletext_text_input.

#include "test_framework.hpp"

#include <firmir/gui/editabletext.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>

#include <cstdlib>
#include <headless_control.hpp>

using namespace Firmir;
using namespace Firmir::Gui;

namespace {

/// Initializes the platform singleton with the headless backend once.
void ensurePlatform() {
    static const bool once = [] {
        testing::setEnv("FIRMIR_GUI_DISPLAY", "headless");
        testing::setEnv("FIRMIR_GUI_INPUT", "headless");
        Platform::instance();
        return true;
    }();
    (void)once;
}

struct EditFixture {
    Window window;
    EditableText* edit;

    EditFixture()
        : window(Platform::instance()->screens().back()) {
        window.setGeometry(Rect{0, 0, 200, 100});
        edit = window.createItem<EditableText>(window.contentItem());
        edit->setGeometry(4, 4, 180, 60);
        window.show();
        // Click into the edit to focus it.
        firmir_gui_headless_inject_mouse(
            static_cast<int>(MouseEvent::Action::Press), 20, 20, 1);
        firmir_gui_headless_inject_mouse(
            static_cast<int>(MouseEvent::Action::Release), 20, 20, 1);
    }
};

void typeText(const char* text, int key = 30) {
    firmir_gui_headless_inject_key(true, key, text);
    firmir_gui_headless_inject_key(false, key, "");
}

} // namespace

TEST_CASE(editabletext_enter_accepts_single_line) {
    ensurePlatform();
    firmir_gui_headless_add_screen("EDIT-ML-1", 200, 100);
    {
        EditFixture f;
        CHECK(f.window.focusItem() == f.edit);

        bool accepted = false;
        f.edit->connect(f.edit, EditableText::signalAccepted,
                        [&accepted] { accepted = true; });
        typeText("ab");
        // Enter on a single-line edit: accepted, no text change.
        typeText("", 28); // KEY_ENTER
        CHECK(accepted);
        CHECK(f.edit->text() == String("ab"));
        CHECK(!f.edit->multiline());
    }
}

TEST_CASE(editabletext_enter_newline_in_multiline) {
    ensurePlatform();
    firmir_gui_headless_add_screen("EDIT-ML-2", 200, 100);
    {
        EditFixture f;
        f.edit->setMultiline(true);
        CHECK(f.edit->multiline());

        bool accepted = false;
        f.edit->connect(f.edit, EditableText::signalAccepted,
                        [&accepted] { accepted = true; });
        typeText("a");
        typeText("", 28); // KEY_ENTER
        typeText("b");
        CHECK(!accepted); // multiline: Enter inserts, does not accept
        CHECK(f.edit->text() == String("a\nb"));
    }
}

TEST_CASE(editabletext_delete_and_backspace_edges) {
    ensurePlatform();
    firmir_gui_headless_add_screen("EDIT-ML-3", 200, 100);
    {
        EditFixture f;
        typeText("ab"); // caret lands after the inserted text
        typeText("", 105); // KEY_LEFT: caret between a|b
        typeText("", 111); // KEY_DELETE: forward-delete removes "b"
        CHECK(f.edit->text() == String("a"));
        typeText("", 14); // KEY_BACKSPACE
        CHECK(f.edit->text().isEmpty());
        // Backspace on empty text: no crash, stays empty.
        typeText("", 14);
        CHECK(f.edit->text().isEmpty());

        // Backspace delivered as text (xkb control char) also deletes.
        typeText("x");
        typeText("\x08", 0);
        CHECK(f.edit->text().isEmpty());

        // Control characters never become text (Tab's "\t" etc.).
        typeText("\t", 15);
        typeText("\x1B", 1); // Escape
        CHECK(f.edit->text().isEmpty());
    }
}

TEST_CASE(editabletext_caret_navigation) {
    ensurePlatform();
    firmir_gui_headless_add_screen("EDIT-NAV-1", 200, 100);
    {
        EditFixture f;
        typeText("abc");
        CHECK(f.edit->cursorPosition() == 3);
        typeText("", 102); // KEY_HOME
        CHECK(f.edit->cursorPosition() == 0);
        typeText("X");
        CHECK(f.edit->text() == String("Xabc"));
        typeText("", 107); // KEY_END
        typeText("Y");
        CHECK(f.edit->text() == String("XabcY"));
        typeText("", 105); // KEY_LEFT
        typeText("", 105);
        CHECK(f.edit->cursorPosition() == 3);
        typeText("", 14); // Backspace removes the char before the caret
        CHECK(f.edit->text() == String("XacY")); // caret between b|c, 'b' removed
    }
}

TEST_CASE(editabletext_shift_selection_and_replace) {
    ensurePlatform();
    firmir_gui_headless_add_screen("EDIT-SEL-1", 200, 100);
    {
        EditFixture f;
        typeText("hello");
        firmir_gui_headless_inject_key(true, 42, ""); // Shift down
        for (int i = 0; i < 3; ++i)
            typeText("", 105); // Shift+Left x3
        firmir_gui_headless_inject_key(false, 42, ""); // Shift up
        CHECK(f.edit->selectionStart() == 2);
        CHECK(f.edit->selectionEnd() == 5);
        CHECK(f.edit->selectedText() == String("llo"));
        // Typing replaces the whole selection at once.
        typeText("p");
        CHECK(f.edit->text() == String("hep"));
        CHECK(f.edit->cursorPosition() == 3);
        CHECK(f.edit->selectionStart() == 3); // no selection: start == caret
    }
}

TEST_CASE(editabletext_selection_delete_at_once) {
    ensurePlatform();
    firmir_gui_headless_add_screen("EDIT-SEL-2", 200, 100);
    {
        EditFixture f;
        typeText("hello");
        // Ctrl+A selects everything; one Backspace deletes it all.
        firmir_gui_headless_inject_key(true, 29, ""); // Ctrl down
        typeText("a", 30);
        firmir_gui_headless_inject_key(false, 29, "");
        CHECK(f.edit->selectedText() == String("hello"));
        typeText("", 14);
        CHECK(f.edit->text().isEmpty());
        CHECK(f.edit->cursorPosition() == 0);

        // Shift+Home selects to the start; Delete removes the selection.
        typeText("abc");
        firmir_gui_headless_inject_key(true, 42, "");
        typeText("", 102); // Shift+Home
        firmir_gui_headless_inject_key(false, 42, "");
        CHECK(f.edit->selectedText() == String("abc"));
        typeText("", 111);
        CHECK(f.edit->text().isEmpty());
    }
}

TEST_CASE(editabletext_selection_collapse_and_escape) {
    ensurePlatform();
    firmir_gui_headless_add_screen("EDIT-SEL-3", 200, 100);
    {
        EditFixture f;
        typeText("ab");
        firmir_gui_headless_inject_key(true, 42, "");
        typeText("", 105); // Shift+Left: "b" selected
        firmir_gui_headless_inject_key(false, 42, "");
        // Plain Left collapses the selection to its start (not one char left).
        typeText("", 105);
        CHECK(f.edit->selectionStart() == 1);
        CHECK(f.edit->cursorPosition() == 1);
        typeText("c");
        CHECK(f.edit->text() == String("acb"));

        // Escape clears the selection without touching the text.
        firmir_gui_headless_inject_key(true, 42, "");
        typeText("", 105); // select "c"
        firmir_gui_headless_inject_key(false, 42, "");
        CHECK(f.edit->selectedText() == String("c"));
        typeText("", 1); // Escape
        CHECK(f.edit->selectedText().isEmpty());
        typeText("d");
        CHECK(f.edit->text() == String("adcb")); // Escape keeps the caret in place
    }
}

TEST_CASE(editabletext_mouse_caret_and_drag_selection) {
    ensurePlatform();
    firmir_gui_headless_add_screen("EDIT-MOUSE-1", 200, 100);
    {
        EditFixture f;
        typeText("abc");
        // Click at the very left edge: caret to the start.
        firmir_gui_headless_inject_mouse(
            static_cast<int>(MouseEvent::Action::Press), 6, 20, 1);
        firmir_gui_headless_inject_mouse(
            static_cast<int>(MouseEvent::Action::Release), 6, 20, 1);
        CHECK(f.edit->cursorPosition() == 0);
        typeText("X");
        CHECK(f.edit->text() == String("Xabc"));
        // Drag from past the text end to the left edge selects everything.
        firmir_gui_headless_inject_mouse(
            static_cast<int>(MouseEvent::Action::Press), 180, 20, 1);
        firmir_gui_headless_inject_mouse(
            static_cast<int>(MouseEvent::Action::Move), 6, 20, 1);
        firmir_gui_headless_inject_mouse(
            static_cast<int>(MouseEvent::Action::Release), 6, 20, 1);
        CHECK(f.edit->selectedText() == String("Xabc"));
        typeText("Z");
        CHECK(f.edit->text() == String("Z"));
    }
}
