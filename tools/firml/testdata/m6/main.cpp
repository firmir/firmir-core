// M6 test app: the codegen target (firml_add_target) compiles app.fml at
// build time; this binary links the generated library and checks the result.
#include "app.hpp"

#include <firmir/gui/animation.hpp>
#include <firmir/gui/rectangle.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <any>
#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void checkEq(const char* name, float actual, float expected) {
    if (std::fabs(actual - expected) > 0.01f) {
        std::printf("FAIL %s: %.2f != %.2f\n", name, actual, expected);
        ++failures;
    }
}

} // namespace

int main() {
    AppObjects o = createApp(nullptr);

    checkEq("box.width bound", o.box->width(), 50.0f);
    o.root->setProperty("width", std::any(200.0));
    checkEq("box.width rebound", o.box->width(), 100.0f);

    // Behavior: animated write via the generated library.
    Firmir::Gui::Animation* behavior = o.box->behavior("height");
    if (!behavior) {
        std::printf("FAIL no behavior on box.height\n");
        ++failures;
    } else {
        bool finished = false;
        behavior->connect(
            o.box, Firmir::Gui::Animation::signalFinished,
            [&] {
                finished = true;
                Firmir::Thread::mainThread()->quit();
            },
            Firmir::ConnectionMode::Direct);
        Firmir::Timer::singleShot(std::chrono::milliseconds(5000), o.box,
                                  [] { Firmir::Thread::mainThread()->quit(); });
        o.box->setProperty("height", std::any(30.0));
        if (!behavior->running()) {
            std::printf("FAIL behavior did not consume the write\n");
            ++failures;
        }
        Firmir::Thread::mainThread()->exec();
        if (!finished) {
            std::printf("FAIL behavior animation never finished\n");
            ++failures;
        }
        checkEq("box.height animated", o.box->height(), 30.0f);
    }

    delete o.root;
    if (failures) {
        std::printf("m6 app: %d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("m6 app: all checks passed\n");
    return 0;
}
