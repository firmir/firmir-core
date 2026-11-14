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
#include <firmir/timer.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>

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

    // FIRMIR_EXAMPLE_TIMER_MS overrides the timer interval (default 1000 ms).
    // The move itself takes 500 ms, so the default leaves the window idle for
    // half of every cycle; a value at or below 500 makes the move continuous,
    // which tells an engine stall apart from the idle gap between moves
    // (FIRMIR_GUI_FRAME_TRACE=1 shows both as a large "frame gap").
    if (const char* value = std::getenv("FIRMIR_EXAMPLE_TIMER_MS")) {
        const int interval = std::atoi(value);
        if (interval > 0)
            objects.mainTimer->setInterval(
                std::chrono::milliseconds(interval));
    }

    const int rc = Firmir::Thread::mainThread()->exec();
    delete objects.root; // owns the whole tree (items, timer, app singleton)
    return rc;
}
