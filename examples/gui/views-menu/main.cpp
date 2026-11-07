// views-menu example: the whole UI (menu model + view switching) is declared
// in views_menu.fml (compiled to C++ by firmlc at build time); this file only
// picks the screen and runs the event loop.

#include "views_menu.hpp"

#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/log.hpp>
#include <firmir/thread.hpp>

#include <cstdio>

int main() {
    Firmir::Gui::Platform* platform = Firmir::Gui::Platform::instance();
    if (platform->screens().empty()) {
        std::fprintf(stderr,
                     "no screens — check FIRMIR_GUI_DISPLAY / "
                     "FIRMIR_GUI_PLUGIN_PATH\n");
        return 1;
    }
    Firmir::Gui::Screen* screen = platform->screens().front();
    Firmir::logInfo() << "screen:" << screen->name() << screen->size().width
                      << screen->size().height;

    // Creates the window, the item tree (menu repeater + views) and shows the
    // window (see views_menu.fml).
    ViewsMenuObjects objects = createViewsMenu(screen);

    const int rc = Firmir::Thread::mainThread()->exec();
    delete objects.root; // owns the whole tree
    return rc;
}
