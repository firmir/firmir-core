// E2E for generated signal/property handlers, singleton methods and Button.
#include "handlers.h"

#include <firmir/gui/app.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/string.hpp>
#include <headless_control.hpp>

#include <cstdio>
#include <cstdlib>

#define CHECK(x)                                                                  \
    do {                                                                          \
        if (!(x)) {                                                               \
            std::fprintf(stderr, "handlers e2e: CHECK failed at line %d\n", __LINE__); \
            return 1;                                                             \
        }                                                                         \
    } while (false)

int main() {
    ::setenv("FIRMIR_GUI_DISPLAY", "headless", 1);
    ::setenv("FIRMIR_GUI_INPUT", "headless", 1);
    auto* platform = Firmir::Gui::Platform::instance();
    (void)platform;
    firmir_gui_headless_add_screen("HANDLERS-1", 640, 480);
    Firmir::Gui::Window window(platform->screens().front());
    window.resize(300, 300);
    Firmir::Gui::App app(&window);
    Firmir::Object::registerSingleton("Firmir.Gui.App", &app);
    HandlersObjects objects = createHandlers(window.contentItem());
    window.show();

    objects.root->emitSignal<int, Firmir::String>("someSignal", 7,
                                                   Firmir::String("typed title"));
    CHECK(window.title() == "typed title");
    objects.root->setWidth(320);
    CHECK(window.title() == "width changed");

    const int x = 150;
    const int y = 150;
    firmir_gui_headless_inject_mouse(
        static_cast<int>(Firmir::Gui::MouseEvent::Action::Press), x, y, 1);
    CHECK(objects.closeButton->isPressed());
    firmir_gui_headless_inject_mouse(
        static_cast<int>(Firmir::Gui::MouseEvent::Action::Release), x, y, 1);
    CHECK(!objects.closeButton->isPressed());
    CHECK(!window.isVisible());

    firmir_gui_headless_remove_screen("HANDLERS-1");
    std::puts("handlers e2e: OK");
    return 0;
}
