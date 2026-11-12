// E2E for script-declared properties + string concatenation (M9): the
// generated createCalcApp() builds a mini calculator; the test pushes its
// buttons by emitting "clicked" and checks the bound display text. Runs on
// the headless backend (all propertyChanged connections are Direct, so no
// event loop is needed).
#include "calc_app.hpp"

#include <firmir/gui/button.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/text.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/object.hpp>
#include <headless_control.hpp>

#include <cstdio>
#include <cstdlib>

#define CHECK(x)                                                                  \
    do {                                                                          \
        if (!(x)) {                                                               \
            std::fprintf(stderr, "calc app e2e: CHECK failed at line %d\n",       \
                         __LINE__);                                               \
            return 1;                                                             \
        }                                                                         \
    } while (false)

int main() {
    #ifdef _WIN32
      ::_putenv("FIRMIR_GUI_DISPLAY=headless");
      ::_putenv("FIRMIR_GUI_INPUT=headless");
    #else
      ::setenv("FIRMIR_GUI_DISPLAY", "headless", 1);
      ::setenv("FIRMIR_GUI_INPUT", "headless", 1);
    #endif
    auto* platform = Firmir::Gui::Platform::instance();
    (void)platform;
    firmir_gui_headless_add_screen("CALC-1", 800, 600);

    CalcAppObjects objects = createCalcApp(platform->screens().front());
    CHECK(objects.root != nullptr);
    CHECK(objects.root->isVisible());

    // Initial state: entry 0, editing -> the display shows "0".
    CHECK(objects.display->text() == Firmir::String("0"));

    // "1", "2" -> the display shows "12" (entry typed digit by digit).
    objects.btn1->emitSignal("clicked");
    objects.btn2->emitSignal("clicked");
    CHECK(objects.display->text() == Firmir::String("12"));
    CHECK(Firmir::detail::anyConvert<double>(
              objects.root->getProperty("entry")) == 12.0);

    // "+" -> the running total (12) is displayed.
    objects.btnPlus->emitSignal("clicked");
    CHECK(objects.display->text() == Firmir::String("12"));

    // "3", "=" -> 12 + 3 = 15.
    objects.btn3->emitSignal("clicked");
    CHECK(objects.display->text() == Firmir::String("3"));
    objects.btnEq->emitSignal("clicked");
    CHECK(objects.display->text() == Firmir::String("15"));
    CHECK(Firmir::detail::anyConvert<double>(objects.root->getProperty("acc")) ==
          15.0);

    // "C" resets everything.
    objects.btnC->emitSignal("clicked");
    CHECK(objects.display->text() == Firmir::String("0"));

    firmir_gui_headless_remove_screen("CALC-1");
    delete objects.root; // owns the tree
    std::puts("calc app e2e: OK");
    return 0;
}
