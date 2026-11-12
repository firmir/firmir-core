// E2E for script functions: a value function initializes a binding, the
// binding re-evaluates when the function's deps change, and a void function
// mutates the owner from a handler command.
#include "function.h"

#include <firmir/gui/button.hpp>
#include <firmir/gui/rectangle.hpp>
#include <firmir/gui/text.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/window.hpp>
#include <headless_control.hpp>

#include <any>
#include <cstdio>
#include <cstdlib>

#define CHECK(x)                                                                  \
    do {                                                                          \
        if (!(x)) {                                                               \
            std::fprintf(stderr, "function e2e: CHECK failed at line %d\n",       \
                         __LINE__);                                               \
            return 1;                                                             \
        }                                                                         \
    } while (false)

namespace {

double numberProp(Firmir::Object* obj, const char* name) {
    return std::any_cast<double>(obj->getProperty(name));
}

} // namespace

int main() {
    #ifdef _WIN32
      ::_putenv("FIRMIR_GUI_DISPLAY=headless");
      ::_putenv("FIRMIR_GUI_INPUT=headless");
    #else
      ::setenv("FIRMIR_GUI_DISPLAY", "headless", 1);
      ::setenv("FIRMIR_GUI_INPUT", "headless", 1);
    #endif
    auto* platform = Firmir::Gui::Platform::instance();
    firmir_gui_headless_add_screen("FUNCTION-1", 640, 480);
    Firmir::Gui::Window window(platform->screens().front());
    window.resize(640, 480);
    FunctionObjects objects = createFunction(window.contentItem());
    window.show();

    // Static call: greet("World") initialized the text at creation.
    CHECK(objects.title->text() == Firmir::String("Hello, World!"));
    // Bound call: x == doubleClicks() == clicks * 2 == 0.
    CHECK(objects.title->x() == 0.0f);
    CHECK(objects.root->width() == 300.0f);
    CHECK(objects.root->height() == 200.0f);

    // First click: the handler increments clicks, calls the void applySize()
    // and rewrites the text through greet().
    objects.btn->emitSignal("clicked");
    CHECK(numberProp(objects.root, "clicks") == 1.0);
    CHECK(objects.root->width() == 500.0f);
    CHECK(objects.root->height() == 400.0f);
    CHECK(objects.title->text() == Firmir::String("Hello, Firmir!"));

    // The binding on x re-evaluated: doubleClicks() re-exported root.clicks.
    CHECK(objects.title->x() == 2.0f);

    // Second click: the dep chain keeps the binding live.
    objects.btn->emitSignal("clicked");
    CHECK(numberProp(objects.root, "clicks") == 2.0);
    CHECK(objects.title->x() == 4.0f);

    firmir_gui_headless_remove_screen("FUNCTION-1");
    std::puts("function e2e: OK");
    return 0;
}
