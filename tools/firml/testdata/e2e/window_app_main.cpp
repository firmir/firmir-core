// E2E for Window-rooted Firml codegen (M8): the generated createWindowApp()
// builds the window on the given screen, registers the Firmir.Gui.App
// singleton, binds the window size to the screen, and a Timer moves the box
// through animated handler assignments. Runs on the headless backend.
#include "window_app.hpp"

#include <firmir/gui/animation.hpp>
#include <firmir/gui/app.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/object.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>
#include <headless_control.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>

#define CHECK(x)                                                                  \
    do {                                                                          \
        if (!(x)) {                                                               \
            std::fprintf(stderr, "window app e2e: CHECK failed at line %d\n",     \
                         __LINE__);                                               \
            return 1;                                                             \
        }                                                                         \
    } while (false)

int main() {
    ::setenv("FIRMIR_GUI_DISPLAY", "headless", 1);
    ::setenv("FIRMIR_GUI_INPUT", "headless", 1);
    auto* platform = Firmir::Gui::Platform::instance();
    (void)platform;
    firmir_gui_headless_add_screen("WIN-1", 640, 480);

    WindowAppObjects objects = createWindowApp(platform->screens().front());
    CHECK(objects.root != nullptr);
    CHECK(objects.root->isVisible());
    // width/height are bound to Firmir.Gui.App.screenWidth/screenHeight / 2.
    CHECK(objects.root->geometry().width == 320);
    CHECK(objects.root->geometry().height == 240);
    CHECK(objects.root->title() == "window app e2e");

    // The singleton was created and registered by the generated code.
    auto* app = Firmir::Object::singleton<Firmir::Gui::App>("Firmir.Gui.App");
    CHECK(app != nullptr);
    CHECK(app == objects.app);
    CHECK(app->window() == objects.root);
    CHECK(app->screenWidth() == 640.0);
    CHECK(app->screenHeight() == 480.0);

    CHECK(objects.box != nullptr);
    CHECK(objects.box->x() == 5.0f);
    CHECK(objects.box->behavior("x") != nullptr);
    CHECK(objects.ticker != nullptr);
    CHECK(objects.ticker->isActive());
    CHECK(objects.ticker->interval() == std::chrono::milliseconds(50));

    // Let the 50 ms ticker run: every timeout does box.x = box.x + 10, and
    // the 20 ms behavior animates each write.
    Firmir::Timer::singleShot(std::chrono::milliseconds(700), objects.root,
                              [] { Firmir::Thread::mainThread()->quit(); });
    Firmir::Thread::mainThread()->exec();

    // ~13 ticks => box.x is far from the initial 5 (conservative bound).
    CHECK(objects.box->x() > 60.0f);

    firmir_gui_headless_remove_screen("WIN-1");
    delete objects.root; // owns the tree, the timer and the App singleton
    std::puts("window app e2e: OK");
    return 0;
}
