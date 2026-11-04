// Firmir demo application: selectable generated views and a Close button.
// Run: FIRMIR_GUI_DISPLAY=wayland ./red-window [red|blue]
#include "views/red.h"
#include "views/blue.h"

#include <firmir/gui/app.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/thread.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main(int argc, char** argv) {
    Firmir::Gui::Platform* platform = Firmir::Gui::Platform::instance();
    const std::vector<Firmir::Gui::Screen*> screens = platform->screens();
    if (screens.empty()) {
        std::fprintf(stderr,
                     "red-window: no screens (is a display backend loaded? "
                     "try FIRMIR_GUI_DISPLAY=wayland)\n");
        return 1;
    }
    std::fprintf(stderr, "red-window: screen %s %dx%d\n",
                 screens.front()->name().c_str(), screens.front()->size().width,
                 screens.front()->size().height);

    Firmir::Gui::Window window(screens.front());
    window.setTitle("Firml - generated views");
    window.resize(300, 300);
    Firmir::Gui::App app(&window);
    Firmir::Object::registerSingleton("Firmir.Gui.App", &app);

    const char* view = argc > 1 ? argv[1] : std::getenv("RED_WINDOW_VIEW");
    if (view && std::strcmp(view, "blue") == 0)
        createViewsBlue(window.contentItem());
    else
        createViewsRed(window.contentItem());

    window.connect<bool>(
        &window, Firmir::Gui::Window::signalVisibilityChanged,
        [](bool visible) {
            if (!visible)
                Firmir::Thread::mainThread()->quit();
        },
        Firmir::ConnectionMode::Direct);

    window.show();
    return Firmir::Thread::mainThread()->exec();
}
