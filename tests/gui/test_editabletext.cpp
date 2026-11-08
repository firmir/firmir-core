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
        ::setenv("FIRMIR_GUI_DISPLAY", "headless", 1);
        ::setenv("FIRMIR_GUI_INPUT", "headless", 1);
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
        typeText("ab");
        typeText("", 111); // KEY_DELETE: erases the last character
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
