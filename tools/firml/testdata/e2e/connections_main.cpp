// E2E for the QML-style Connections/Binding constructs: the generated code
// must wire handlers and dependency-tracked bindings to the target object.
#include "connections.h"

#include <firmir/gui/button.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/rectangle.hpp>
#include <firmir/gui/window.hpp>
#include <headless_control.hpp>

#include <cstdio>

#define CHECK(x)                                                                  \
    do {                                                                          \
        if (!(x)) {                                                               \
            std::fprintf(stderr, "connections e2e: CHECK failed at line %d\n",    \
                         __LINE__);                                               \
            return 1;                                                             \
        }                                                                         \
    } while (false)

int main() {
    ::setenv("FIRMIR_GUI_DISPLAY", "headless", 1);
    ::setenv("FIRMIR_GUI_INPUT", "headless", 1);
    auto* platform = Firmir::Gui::Platform::instance();
    (void)platform;
    firmir_gui_headless_add_screen("CONN-1", 640, 480);
    Firmir::Gui::Window window(platform->screens().front());
    window.resize(300, 300);
    ConnectionsObjects objects = createConnections(window.contentItem());
    window.show();

    // Binding (static): x was forced to 40 although the object says x: 10.
    CHECK(objects.grow->x() == 40.0f);
    // Binding (tracked): width <= 200 -> "small".
    CHECK(objects.grow->text().toStdString() == "small");

    // Click the button: the Connections handler grows the root...
    firmir_gui_headless_inject_mouse(
        static_cast<int>(Firmir::Gui::MouseEvent::Action::Press), 50, 20, 1);
    firmir_gui_headless_inject_mouse(
        static_cast<int>(Firmir::Gui::MouseEvent::Action::Release), 50, 20, 1);
    CHECK(objects.root->width() == 210.0f);
    // ...which re-evaluates the Binding through its root.width dependency.
    CHECK(objects.grow->text().toStdString() == "big");

    firmir_gui_headless_remove_screen("CONN-1");
    std::puts("connections e2e: OK");
    return 0;
}
