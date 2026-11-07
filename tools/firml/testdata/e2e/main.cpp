// E2E test driver for firmlc-generated code: builds the app.fml item tree,
// checks the anchor-computed geometry and live updates, exits non-zero on
// the first mismatch count > 0.

#include "app.hpp"

#include <firmir/gui/animation.hpp>
#include <firmir/gui/rectangle.hpp>
#include <firmir/gui/text.hpp>
#include <firmir/string.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <any>
#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void checkEq(const char* what, float actual, float expected) {
    if (std::fabs(actual - expected) > 0.001f) {
        std::printf("FAIL %s: %.2f != %.2f\n", what, actual, expected);
        ++failures;
    }
}

} // namespace

int main() {
    AppObjects o = createApp(nullptr);

    // Static properties.
    checkEq("root.width", o.root->width(), 200.0f);
    checkEq("root.height", o.root->height(), 100.0f);
    if (std::any_cast<Firmir::Gui::Color>(o.root->getProperty("color")) !=
        Firmir::Gui::Color::rgb(0x10, 0x20, 0x30)) {
        std::printf("FAIL root.color\n");
        ++failures;
    }
    if (std::any_cast<Firmir::String>(o.label->getProperty("text")) !=
        Firmir::String("narrow")) { // binding: root.width > 250 ? "wide" : "narrow"
        std::printf("FAIL label.text\n");
        ++failures;
    }

    // Initial anchor geometry (right.width is the binding left.width * 2 = 80).
    checkEq("right.width", o.right->width(), 80.0f);
    checkEq("right.x", o.right->x(), 60.0f);  // 10 + 40 + 10
    checkEq("right.y", o.right->y(), 10.0f);  // (10 + 10) - 10
    checkEq("label.x", o.label->x(), 145.0f); // 200 - 5 - 50
    checkEq("label.y", o.label->y(), 90.0f);  // 100 - 10
    checkEq("filler.x", o.filler->x(), 4.0f);
    checkEq("filler.y", o.filler->y(), 20.0f); // margin binding: left.height
    checkEq("filler.width", o.filler->width(), 192.0f);  // 200 - 8
    checkEq("filler.height", o.filler->height(), 76.0f); // 100 - 20 - 4

    // Live updates through the property system.
    o.root->setProperty("width", std::any(300.0));
    checkEq("filler.width resized", o.filler->width(), 292.0f);
    checkEq("label.x resized", o.label->x(), 245.0f); // 300 - 5 - 50
    if (std::any_cast<Firmir::String>(o.label->getProperty("text")) !=
        Firmir::String("wide")) { // the binding re-evaluated
        std::printf("FAIL label.text after resize\n");
        ++failures;
    }

    // left.x has a behavior: writes are animated, so the anchor chain is
    // checked in the async phase below instead.
    o.left->setProperty("width", std::any(50.0));
    checkEq("right.width rebound", o.right->width(), 100.0f); // 50 * 2
    checkEq("right.x rebound", o.right->x(), 70.0f);          // 10 + 50 + 10

    o.left->setProperty("height", std::any(30.0));
    checkEq("filler.y rebound", o.filler->y(), 30.0f);
    checkEq("filler.height rebound", o.filler->height(), 66.0f); // 100 - 30 - 4

    // Behavior: the write is consumed and animated; anchors follow the frames.
    {
        Firmir::Gui::Animation* behavior = o.left->behavior("x");
        if (!behavior) {
            std::printf("FAIL no behavior on left.x\n");
            ++failures;
        } else {
            bool finished = false;
            behavior->connect(
                o.left, Firmir::Gui::Animation::signalFinished,
                [&] {
                    finished = true;
                    Firmir::Thread::mainThread()->quit();
                },
                Firmir::ConnectionMode::Direct);
            Firmir::Timer::singleShot(std::chrono::milliseconds(5000), o.left, [] {
                Firmir::Thread::mainThread()->quit();
            });
            o.left->setProperty("x", std::any(60.0));
            if (!behavior->running()) {
                std::printf("FAIL behavior did not consume the write\n");
                ++failures;
            }
            Firmir::Thread::mainThread()->exec();
            if (!finished) {
                std::printf("FAIL behavior animation never finished\n");
                ++failures;
            }
            checkEq("left.x animated", o.left->x(), 60.0f);
            checkEq("right.x after animation", o.right->x(), 120.0f); // 60+50+10
        }
    }

    // ids are exposed through the object name as well.
    if (o.root->findChild<Firmir::Gui::Text>("label") != o.label) {
        std::printf("FAIL findChild(label)\n");
        ++failures;
    }

    delete o.root; // owns the whole tree
    if (failures > 0) {
        std::printf("e2e app: %d check(s) FAILED\n", failures);
        return 1;
    }
    std::printf("e2e app: OK\n");
    return 0;
}
