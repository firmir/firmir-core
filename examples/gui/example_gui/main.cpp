// firmir-gui example: the whole UI is declared in example_gui.fml (compiled
// to C++ by firmlc at build time); this file only picks the screen and runs
// the event loop.
//
// Backend selection:
//   FIRMIR_GUI_DISPLAY=drm|headless   display backend plugin
//   FIRMIR_GUI_INPUT=evdev|headless   input backend plugin
//   FIRMIR_GUI_PLUGIN_PATH=...        plugin search path (colon-separated)

#include "example_gui.hpp"

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

    // Creates the window, the item tree and the Firmir.Gui.App singleton,
    // then shows the window (see example_gui.fml).
    ExampleGuiObjects objects = createExampleGui(screen);

    const int rc = Firmir::Thread::mainThread()->exec();
    delete objects.root; // owns the whole tree (items, timer, app singleton)
    return rc;
}
