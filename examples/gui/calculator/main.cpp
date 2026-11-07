// firmir-gui calculator: the whole UI and logic are declared in
// calculator.fml (compiled to C++ by firmlc at build time); this file only
// picks the screen and runs the event loop.
//
// Backend selection:
//   FIRMIR_GUI_DISPLAY=drm|wayland|headless   display backend plugin
//   FIRMIR_GUI_INPUT=evdev|headless           input backend plugin
//   FIRMIR_GUI_PLUGIN_PATH=...                plugin search path (colon-separated)

#include "calculator.hpp"

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

    // Creates the window, the button tree and the calculator state, then
    // shows the window (see calculator.fml).
    CalculatorObjects objects = createCalculator(screen);

    const int rc = Firmir::Thread::mainThread()->exec();
    delete objects.root; // owns the whole tree
    return rc;
}
