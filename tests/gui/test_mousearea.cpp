// MouseArea behavior through injected window input: press/release state,
// position tracking, hover (entered/exited/containsMouse), clicked signal.

#include "test_framework.hpp"

#include <firmir/gui/mousearea.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>

#include <cmath>
#include <cstdlib>
#include <headless_control.hpp>

using namespace Firmir;
using namespace Firmir::Gui;

namespace {

void ensurePlatform() {
    static const bool once = [] {
        testing::setEnv("FIRMIR_GUI_DISPLAY", "headless");
        testing::setEnv("FIRMIR_GUI_INPUT", "headless");
        Platform::instance();
        return true;
    }();
    (void)once;
}

void mouse(MouseEvent::Action action, int x, int y, int button = 1) {
    firmir_gui_headless_inject_mouse(static_cast<int>(action), x, y, button);
}

void touch(TouchEvent::Action action, int id, float x, float y) {
    firmir_gui_headless_inject_touch(static_cast<int>(action), id, x, y);
}

} // namespace

TEST_CASE(mousearea_press_position_release) {
    ensurePlatform();
    firmir_gui_headless_add_screen("MA-1", 200, 100);
    {
        Window window(Platform::instance()->screens().back());
        window.setGeometry(Rect{0, 0, 200, 100});
        auto* area = window.createItem<MouseArea>(window.contentItem());
        area->setGeometry(10, 10, 100, 60);
        window.show();

        int clicks = 0;
        int presses = 0;
        int releases = 0;
        area->connect<MouseEvent>(area, MouseArea::signalClicked,
                        [&clicks](MouseEvent) { ++clicks; },
                        ConnectionMode::Direct);
        area->connect<MouseEvent>(area, MouseArea::signalPressed,
                        [&presses](MouseEvent) { ++presses; },
                        ConnectionMode::Direct);
        area->connect<MouseEvent>(area, MouseArea::signalReleased,
                        [&releases](MouseEvent) { ++releases; },
                        ConnectionMode::Direct);

        // Press inside: pressed state, position relative to the area.
        mouse(MouseEvent::Action::Press, 35, 40);
        CHECK(area->isPressed());
        CHECK(std::fabs(area->mouseX() - 25.0f) < 0.01f);
        CHECK(std::fabs(area->mouseY() - 30.0f) < 0.01f);
        CHECK_EQ(presses, 1);

        // Move while pressed: position tracks.
        mouse(MouseEvent::Action::Move, 55, 50);
        CHECK(std::fabs(area->mouseX() - 45.0f) < 0.01f);
        CHECK(std::fabs(area->mouseY() - 40.0f) < 0.01f);

        // Release: click emitted (press+release inside).
        mouse(MouseEvent::Action::Release, 55, 50);
        CHECK(!area->isPressed());
        CHECK_EQ(releases, 1);
        CHECK_EQ(clicks, 1);

        // Press and release far outside: no interaction.
        mouse(MouseEvent::Action::Press, 190, 95);
        mouse(MouseEvent::Action::Release, 190, 95);
        CHECK(!area->isPressed());
        CHECK_EQ(clicks, 1);
    }
}

TEST_CASE(mousearea_hover) {
    ensurePlatform();
    firmir_gui_headless_add_screen("MA-2", 200, 100);
    {
        Window window(Platform::instance()->screens().back());
        window.setGeometry(Rect{0, 0, 200, 100});
        auto* area = window.createItem<MouseArea>(window.contentItem());
        area->setGeometry(10, 10, 100, 60);
        area->setHoverEnabled(true);
        window.show();

        int entered = 0;
        int exited = 0;
        area->connect<>(area, MouseArea::signalEntered,
                        [&entered] { ++entered; }, ConnectionMode::Direct);
        area->connect<>(area, MouseArea::signalExited,
                        [&exited] { ++exited; }, ConnectionMode::Direct);

        CHECK(!area->containsMouse());
        mouse(MouseEvent::Action::Move, 30, 30);
        CHECK(area->containsMouse());
        CHECK_EQ(entered, 1);
        CHECK_EQ(exited, 0);
        mouse(MouseEvent::Action::Move, 190, 95);
        CHECK(!area->containsMouse());
        CHECK_EQ(exited, 1);
    }
}

TEST_CASE(mousearea_touch_drag_release_outside) {
    ensurePlatform();
    firmir_gui_headless_add_screen("MA-4", 200, 100);
    {
        Window window(Platform::instance()->screens().back());
        window.setGeometry(Rect{0, 0, 200, 100});
        auto* area = window.createItem<MouseArea>(window.contentItem());
        area->setGeometry(10, 10, 100, 60);
        window.show();

        int clicks = 0;
        int releases = 0;
        area->connect<MouseEvent>(area, MouseArea::signalClicked,
                        [&clicks](MouseEvent) { ++clicks; },
                        ConnectionMode::Direct);
        area->connect<MouseEvent>(area, MouseArea::signalReleased,
                        [&releases](MouseEvent) { ++releases; },
                        ConnectionMode::Direct);

        // Touch press on the button, finger dragged out, released outside:
        // the contact stays with the area, so pressed must be cleared and
        // no click is emitted.
        touch(TouchEvent::Action::Begin, 1, 30, 30);
        CHECK(area->isPressed());
        touch(TouchEvent::Action::Update, 1, 150, 90);
        CHECK(area->isPressed());
        CHECK(area->mouseX() > 100.0f); // position still tracks the finger
        touch(TouchEvent::Action::End, 1, 150, 90);
        CHECK(!area->isPressed());
        CHECK_EQ(releases, 1);
        CHECK_EQ(clicks, 0);

        // Press and release both inside still clicks.
        touch(TouchEvent::Action::Begin, 2, 30, 30);
        touch(TouchEvent::Action::End, 2, 30, 30);
        CHECK(!area->isPressed());
        CHECK_EQ(clicks, 1);
        CHECK_EQ(releases, 2);
    }
    firmir_gui_headless_remove_screen("MA-4");
}

TEST_CASE(mousearea_touch_release_outside_window) {
    ensurePlatform();
    firmir_gui_headless_add_screen("MA-5", 400, 200);
    {
        Window window(Platform::instance()->screens().back());
        window.setGeometry(Rect{0, 0, 200, 100});
        auto* area = window.createItem<MouseArea>(window.contentItem());
        area->setGeometry(10, 10, 100, 60);
        window.show();

        int clicks = 0;
        area->connect<MouseEvent>(area, MouseArea::signalClicked,
                        [&clicks](MouseEvent) { ++clicks; },
                        ConnectionMode::Direct);

        // The finger leaves the window entirely: the contact still ends on
        // the window (and the area) that accepted it.
        touch(TouchEvent::Action::Begin, 3, 30, 30);
        CHECK(area->isPressed());
        touch(TouchEvent::Action::Update, 3, 350, 180);
        touch(TouchEvent::Action::End, 3, 350, 180);
        CHECK(!area->isPressed());
        CHECK_EQ(clicks, 0);

        // Mouse drag out of the window: same, release is not lost.
        mouse(MouseEvent::Action::Press, 30, 30);
        CHECK(area->isPressed());
        mouse(MouseEvent::Action::Move, 350, 180);
        mouse(MouseEvent::Action::Release, 350, 180);
        CHECK(!area->isPressed());
        CHECK_EQ(clicks, 0);

        // A press outside the window is ignored and does not steal the grab.
        mouse(MouseEvent::Action::Press, 350, 180);
        mouse(MouseEvent::Action::Release, 350, 180);
        CHECK(!area->isPressed());
        CHECK_EQ(clicks, 0);
    }
    firmir_gui_headless_remove_screen("MA-5");
}

TEST_CASE(mousearea_accepted_buttons) {
    ensurePlatform();
    firmir_gui_headless_add_screen("MA-3", 200, 100);
    {
        Window window(Platform::instance()->screens().back());
        window.setGeometry(Rect{0, 0, 200, 100});
        auto* area = window.createItem<MouseArea>(window.contentItem());
        area->setGeometry(10, 10, 100, 60);
        window.show();

        CHECK_EQ(area->acceptedButtons(), 1); // left by default
        // Right button is ignored by default.
        mouse(MouseEvent::Action::Press, 30, 30, 2);
        CHECK(!area->isPressed());

        area->setAcceptedButtons(1 | 2);
        mouse(MouseEvent::Action::Press, 30, 30, 2);
        CHECK(area->isPressed());
        mouse(MouseEvent::Action::Release, 30, 30, 2);
        CHECK(!area->isPressed());
    }
}
