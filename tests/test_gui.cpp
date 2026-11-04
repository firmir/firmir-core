// firmir-gui tests, driven through the headless backend plugin.

#include "test_framework.hpp"

#include <firmir/gui/animation.hpp>
#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/rectangle.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/text.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#ifdef FIRMIR_RENDER_TEST_SOFT
#include <firmir/render/render.hpp>
#endif

#include <atomic>
#include <cstdlib>
#include <cmath>
#include <headless_control.hpp>
#include <thread>
#include <vector>

using namespace Firmir;
using namespace Firmir::Gui;

namespace {

/// Initializes the platform singleton with the headless backend once.
void ensurePlatform() {
    static const bool once = [] {
        ::setenv("FIRMIR_GUI_DISPLAY", "headless", 1);
        ::setenv("FIRMIR_GUI_INPUT", "headless", 1);
        Platform::instance();
        return true;
    }();
    (void)once;
}

class ColorItem : public Item {
public:
    ColorItem(Object* parent, Color color) : Item(parent), m_color(color) {}

    static std::atomic<bool> paintedOffMainThread;
    static std::thread::id mainThreadId;

protected:
    void paint(Painter& painter) override {
        painter.fillRect(RectF{0, 0, width(), height()}, m_color);
        if (std::this_thread::get_id() != mainThreadId)
            paintedOffMainThread = true;
    }

private:
    Color m_color;
};

std::atomic<bool> ColorItem::paintedOffMainThread{false};
std::thread::id ColorItem::mainThreadId;

class HandlerItem : public Item {
public:
    using Item::Item;

    int mousePresses = 0;
    int mouseMoves = 0;
    Point lastMouseLocal;
    int keys = 0;
    std::string lastText;
    int touches = 0;
    TouchEvent::Action lastTouchAction = TouchEvent::Action::End;
    int focusIn = 0;
    int focusOut = 0;
    bool acceptInput = true;

protected:
    void mouseEvent(MouseEvent& event) override {
        if (!acceptInput)
            return;
        if (event.action == MouseEvent::Action::Press) {
            ++mousePresses;
            lastMouseLocal = event.pos;
            event.accepted = true;
        } else if (event.action == MouseEvent::Action::Move) {
            ++mouseMoves;
            event.accepted = true;
        }
    }
    void keyEvent(KeyEvent& event) override {
        if (!acceptInput || !event.pressed)
            return;
        ++keys;
        lastText = event.text;
        event.accepted = true;
    }
    void touchEvent(TouchEvent& event) override {
        ++touches;
        lastTouchAction = event.action;
        event.accepted = true;
    }
    void focusInEvent() override { ++focusIn; }
    void focusOutEvent() override { ++focusOut; }
};

} // namespace

TEST_CASE(gui_screens_hotplug) {
    ensurePlatform();
    Platform* p = Platform::instance();
    const std::size_t before = p->screens().size();

    Screen* added = nullptr;
    bool removedSignal = false;
    auto c1 = p->connect<Screen*>(Platform::signalScreenAdded,
                                  [&](Screen* s) { added = s; });
    auto c2 = p->connect<Screen*>(Platform::signalScreenRemoved,
                                  [&](Screen*) { removedSignal = true; });

    firmir_gui_headless_add_screen("HOTPLUG-1", 800, 600);
    CHECK_EQ(p->screens().size(), before + 1);
    CHECK(added != nullptr);
    CHECK_EQ(added->name(), "HOTPLUG-1");
    CHECK_EQ(added->size().width, 800);
    CHECK_EQ(p->screen("HOTPLUG-1"), added);

    bool screenRemovedSignal = false;
    added->connect(Screen::signalRemoved, [&] { screenRemovedSignal = true; });
    firmir_gui_headless_remove_screen("HOTPLUG-1");
    CHECK_EQ(p->screens().size(), before);
    CHECK(removedSignal);
    CHECK(screenRemovedSignal);
    c1.disconnect();
    c2.disconnect();
}
TEST_CASE(gui_window_render_on_render_thread) {
    ensurePlatform();
    ColorItem::mainThreadId = std::this_thread::get_id();
    firmir_gui_headless_add_screen("RENDER-1", 64, 64);
    Screen* screen = Platform::instance()->screen("RENDER-1");
    CHECK(screen != nullptr);

    const int windowIndex = firmir_gui_headless_window_count();
    {
        Window window(screen);
        window.setTitle("render test");
        window.setGeometry(Rect{0, 0, 64, 64});

        auto* red = window.createItem<ColorItem>(window.contentItem(), Color::rgb(255, 0, 0));
        red->setGeometry(0, 0, 32, 64);
        auto* blue = window.createItem<ColorItem>(window.contentItem(), Color::rgb(0, 0, 255));
        blue->setGeometry(32, 0, 32, 64);
        blue->setOpacity(0.5f);

        window.show();
        CHECK(window.isVisible());
        CHECK(testing::waitFor(
            [&] { return firmir_gui_headless_frame_count(windowIndex) > 0; }));

        int width = 0, height = 0;
        CHECK(firmir_gui_headless_last_frame(windowIndex, nullptr, &width,
                                             &height));
        CHECK_EQ(width, 64);
        CHECK_EQ(height, 64);
        std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) *
                                          height);
        CHECK(firmir_gui_headless_last_frame(windowIndex, pixels.data(),
                                             &width, &height));
        // Left half: opaque red.
        CHECK_EQ(pixels[5 * 64 + 5], 0xFFFF0000u);
        // Right half: blue at 50% opacity over the opaque black background.
        CHECK_EQ(pixels[5 * 64 + 40], 0xFF00007Fu);
        // Painting happened on the render thread, not the GUI thread.
        CHECK(ColorItem::paintedOffMainThread.load());
    }
    firmir_gui_headless_remove_screen("RENDER-1");
}

TEST_CASE(gui_item_tree_and_geometry) {
    ensurePlatform();
    firmir_gui_headless_add_screen("TREE-1", 100, 100);
    Screen* screen = Platform::instance()->screen("TREE-1");
    Window window(screen);
    window.setGeometry(Rect{0, 0, 100, 100});
    window.show();

    auto* a = window.createItem(window.contentItem());
    a->setGeometry(10, 10, 50, 50);
    auto* b = a->createChild();
    b->setGeometry(5, 5, 20, 20);

    CHECK_EQ(b->parentItem(), a);
    CHECK_EQ(a->childItems().size(), 1u);
    const Point scene = b->mapToScene({0, 0});
    CHECK_EQ(scene.x, 15.0f);
    CHECK_EQ(scene.y, 15.0f);
    const Point local = b->mapFromScene({15, 15});
    CHECK_EQ(local.x, 0.0f);
    CHECK_EQ(local.y, 0.0f);

    // Re-parenting via plain C++ setParent().
    b->setParent(window.contentItem());
    CHECK_EQ(b->parentItem(), window.contentItem());
    CHECK_EQ(b->window(), &window);
    const Point scene2 = b->mapToScene({0, 0});
    CHECK_EQ(scene2.x, 5.0f);

    // Opacity composes down the tree.
    a->setOpacity(0.5f);
    b->setParent(a);
    CHECK_EQ(b->effectiveOpacity(), 0.5f);
    CHECK_EQ(a->effectiveOpacity(), 0.5f);

    // Hit testing: b is topmost at (16,16) in scene coordinates; the content
    // item covers the whole window and is the ultimate fallback.
    CHECK_EQ(window.contentItem()->itemAt(16, 16), b);
    CHECK_EQ(window.contentItem()->itemAt(95, 95), window.contentItem());
    CHECK_EQ(window.contentItem()->itemAt(150, 150), nullptr);
    b->setVisible(false);
    CHECK_EQ(window.contentItem()->itemAt(16, 16), a);
    a->setEnabled(false);
    CHECK_EQ(window.contentItem()->itemAt(16, 16), window.contentItem());
    a->setEnabled(true);

    // Detached items have no window and do not crash on update().
    auto* orphan = new Item;
    CHECK(orphan->window() == nullptr);
    orphan->setGeometry(0, 0, 10, 10);
    orphan->setOpacity(0.5f);
    orphan->setVisible(false);
    orphan->setEnabled(false);
    orphan->setFocus(); // no-op without a window
    delete orphan;

    firmir_gui_headless_remove_screen("TREE-1");
}

TEST_CASE(gui_input_mouse_keyboard_focus) {
    ensurePlatform();
    firmir_gui_headless_add_screen("INPUT-1", 200, 200);
    Screen* screen = Platform::instance()->screen("INPUT-1");
    Window window(screen);
    window.setGeometry(Rect{0, 0, 200, 200});
    window.show();

    auto* item = window.createItem<HandlerItem>(window.contentItem());
    item->setGeometry(50, 50, 50, 50);

    // Mouse press inside the item: local coordinates + focus transfer.
    firmir_gui_headless_inject_mouse(
        static_cast<int>(MouseEvent::Action::Press), 60, 60, 1);
    CHECK_EQ(item->mousePresses, 1);
    CHECK_EQ(item->lastMouseLocal.x, 10.0f);
    CHECK_EQ(item->lastMouseLocal.y, 10.0f);
    CHECK_EQ(window.focusItem(), item);
    CHECK(item->hasFocus());
    CHECK_EQ(item->focusIn, 1);

    // Key events go to the focused item.
    firmir_gui_headless_inject_key(true, 30, "a");
    CHECK_EQ(item->keys, 1);
    CHECK_EQ(item->lastText, "a");

    // Move is delivered too.
    firmir_gui_headless_inject_mouse(
        static_cast<int>(MouseEvent::Action::Move), 70, 70, 0);
    CHECK_EQ(item->mouseMoves, 1);

    // A disabled item is skipped by hit testing.
    item->setEnabled(false);
    firmir_gui_headless_inject_mouse(
        static_cast<int>(MouseEvent::Action::Press), 60, 60, 1);
    CHECK_EQ(item->mousePresses, 1);
    item->setEnabled(true);

    // An item that does not accept the press lets it bubble to the parent.
    auto* parent = window.createItem<HandlerItem>(window.contentItem());
    parent->setGeometry(100, 100, 80, 80);
    auto* child = parent->createChild<HandlerItem>();
    child->setGeometry(10, 10, 30, 30);
    child->acceptInput = false;
    firmir_gui_headless_inject_mouse(
        static_cast<int>(MouseEvent::Action::Press), 115, 115, 1);
    CHECK_EQ(child->mousePresses, 0);
    CHECK_EQ(parent->mousePresses, 1);
    CHECK_EQ(parent->lastMouseLocal.x, 15.0f); // local to parent

    // Focus switch fires focusOut/focusIn; focus follows the hit item.
    CHECK_EQ(item->focusOut, 1);
    CHECK_EQ(window.focusItem(), child);
    child->setFocus(false);
    CHECK_EQ(window.focusItem(), nullptr);
    CHECK_EQ(child->focusOut, 1);

    // Click outside every item keeps the current focus and hits nothing.
    firmir_gui_headless_inject_mouse(
        static_cast<int>(MouseEvent::Action::Press), 5, 5, 1);
    CHECK_EQ(parent->mousePresses, 1);

    firmir_gui_headless_remove_screen("INPUT-1");
}

TEST_CASE(gui_input_touch) {
    ensurePlatform();
    firmir_gui_headless_add_screen("TOUCH-1", 200, 200);
    Screen* screen = Platform::instance()->screen("TOUCH-1");
    Window window(screen);
    window.setGeometry(Rect{0, 0, 200, 200});
    window.show();

    auto* item = window.createItem<HandlerItem>(window.contentItem());
    item->setGeometry(0, 0, 200, 200);

    firmir_gui_headless_inject_touch(
        static_cast<int>(TouchEvent::Action::Begin), 1, 30, 40);
    CHECK_EQ(item->touches, 1);
    CHECK(item->lastTouchAction == TouchEvent::Action::Begin);
    CHECK_EQ(window.focusItem(), item);

    firmir_gui_headless_inject_touch(
        static_cast<int>(TouchEvent::Action::Update), 0, 35, 45);
    CHECK_EQ(item->touches, 2);
    CHECK(item->lastTouchAction == TouchEvent::Action::Update);

    firmir_gui_headless_inject_touch(
        static_cast<int>(TouchEvent::Action::End), 0, 35, 45);
    CHECK_EQ(item->touches, 3);
    CHECK(item->lastTouchAction == TouchEvent::Action::End);

    firmir_gui_headless_remove_screen("TOUCH-1");
}

TEST_CASE(gui_window_screen_removed_hides_window) {
    ensurePlatform();
    firmir_gui_headless_add_screen("GONE-1", 100, 100);
    Screen* screen = Platform::instance()->screen("GONE-1");
    Window window(screen);
    window.setGeometry(Rect{0, 0, 100, 100});
    window.show();
    CHECK(window.isVisible());

    bool becameInvisible = false;
    window.connect<bool>(Window::signalVisibilityChanged,
                         [&](bool visible) {
                             if (!visible)
                                 becameInvisible = true;
                         });
    firmir_gui_headless_remove_screen("GONE-1");
    CHECK(!window.isVisible());
    CHECK(becameInvisible);
    CHECK(window.screen() == nullptr);
    // show() without a screen must not create a platform window or crash.
    window.show();
    CHECK(!window.isVisible());
}

// ------------------------- properties / anchors -------------------------
// These tests do not need a platform backend: geometry and the property
// system work on detached items.

TEST_CASE(gui_item_property_system) {
    Item item;
    CHECK(item.hasProperty("width"));
    int notifications = 0;
    item.connect<std::string, std::any>(
        &item, Object::signalPropertyChanged,
        [&](const std::string& name, const std::any& value) {
            ++notifications;
            CHECK_EQ(name, "width");
            CHECK_EQ(std::any_cast<float>(value), 100.0f);
        },
        ConnectionMode::Direct);
    item.setProperty("width", std::any(100.0)); // double -> float conversion
    CHECK_EQ(item.width(), 100.0f);
    CHECK_EQ(notifications, 1);
    // Same value -> no change, no notification.
    item.setProperty("width", std::any(100.0));
    CHECK_EQ(notifications, 1);

    const auto meta = item.propertyMetaList();
    CHECK_EQ(meta.size(), 19u);
    CHECK_EQ(meta[0].name, "x");
    CHECK_EQ(meta[2].name, "width");
    CHECK_EQ(meta[5].name, "visible");
}

TEST_CASE(gui_item_anchors_fill_parent) {
    Item parent;
    parent.setGeometry(0, 0, 200, 100);
    Item child(&parent);
    child.fill(&parent);
    CHECK_EQ(child.x(), 0.0f);
    CHECK_EQ(child.y(), 0.0f);
    CHECK_EQ(child.width(), 200.0f);
    CHECK_EQ(child.height(), 100.0f);

    parent.setWidth(300);
    CHECK_EQ(child.width(), 300.0f);

    child.setMargins(10);
    CHECK_EQ(child.x(), 10.0f);
    CHECK_EQ(child.width(), 280.0f);
    CHECK_EQ(child.y(), 10.0f);
    CHECK_EQ(child.height(), 80.0f);

    child.fill(nullptr);
    parent.setWidth(400);
    CHECK_EQ(child.width(), 280.0f); // anchors cleared: no update
}

TEST_CASE(gui_item_anchors_sibling) {
    Item parent;
    parent.setGeometry(0, 0, 400, 300);
    Item a(&parent);
    a.setGeometry(10, 10, 50, 20);
    Item b(&parent);
    b.setGeometry(0, 0, 30, 10);

    b.setAnchorLeft({&a, AnchorLine::Right});
    b.setLeftMargin(5);
    CHECK_EQ(b.x(), 65.0f); // 10 + 50 + 5
    b.setAnchorTop({&a, AnchorLine::Bottom});
    b.setTopMargin(2);
    CHECK_EQ(b.y(), 32.0f); // 10 + 20 + 2

    a.setX(20);
    CHECK_EQ(b.x(), 75.0f);
    a.setHeight(30);
    CHECK_EQ(b.y(), 42.0f); // 10 + 30 + 2

    CHECK(b.anchorLeft().item == &a);
    CHECK(b.anchorLeft().line == AnchorLine::Right);

    b.setAnchorLeft(AnchorTarget{}); // `undefined` clears the anchor
    a.setX(100);
    CHECK_EQ(b.x(), 75.0f);
}

TEST_CASE(gui_item_anchors_right_center_clear) {
    Item parent;
    parent.setGeometry(0, 0, 200, 100);
    Item c(&parent);
    c.setGeometry(0, 0, 50, 10);

    c.setAnchorRight({&parent, AnchorLine::Right});
    c.setRightMargin(10);
    CHECK_EQ(c.x(), 140.0f); // 200 - 10 - 50
    parent.setWidth(300);
    CHECK_EQ(c.x(), 240.0f);

    c.clearAnchors();
    c.setAnchorHorizontalCenter({&parent, AnchorLine::HorizontalCenter});
    c.setHorizontalCenterOffset(4);
    CHECK_EQ(c.x(), 129.0f); // 150 - 25 + 4
    c.setAnchorVerticalCenter({&parent, AnchorLine::VerticalCenter});
    CHECK_EQ(c.y(), 45.0f); // 50 - 5

    Item d(&parent);
    d.setGeometry(0, 0, 20, 20);
    d.centerIn(&parent);
    CHECK_EQ(d.x(), 140.0f); // 150 - 10
    CHECK_EQ(d.y(), 40.0f);  // 50 - 10
    d.centerIn(nullptr);
    parent.setHeight(200);
    CHECK_EQ(d.y(), 40.0f); // unchanged after clear
}

TEST_CASE(gui_item_anchors_target_destroyed) {
    Item parent;
    parent.setGeometry(0, 0, 100, 100);
    auto* a = new Item(&parent);
    a->setGeometry(0, 0, 10, 10);
    Item b(&parent);
    b.setAnchorLeft({a, AnchorLine::Right});
    CHECK_EQ(b.x(), 10.0f);
    delete a;
    CHECK(b.anchorLeft().item == nullptr); // dropped via the destroyed signal
}

TEST_CASE(gui_rectangle_text_properties) {
    Rectangle rect;
    int notifications = 0;
    rect.connect<std::string, std::any>(
        &rect, Object::signalPropertyChanged,
        [&](const std::string&, const std::any&) { ++notifications; },
        ConnectionMode::Direct);
    rect.setProperty("color", std::any(Color::rgb(10, 20, 30)));
    CHECK(rect.color() == Color::rgb(10, 20, 30));
    CHECK_EQ(notifications, 1);
    rect.setProperty("color", std::any(std::string("#FF0000")));
    CHECK(rect.color() == Color::rgb(255, 0, 0));
    CHECK_EQ(notifications, 2);
    bool sawColor = false;
    for (const auto& meta : rect.propertyMetaList())
        if (meta.name == "color")
            sawColor = true;
    CHECK(sawColor);

    Text text;
    text.setProperty("text", std::any(String("Hello")));
    CHECK(text.text() == String("Hello"));
    text.setProperty("color", std::any(Color::rgb(1, 2, 3)));
    CHECK(text.color() == Color::rgb(1, 2, 3));
}

TEST_CASE(gui_color_helpers) {
    CHECK(Color::fromString("#102030") == Color::rgb(0x10, 0x20, 0x30));
    CHECK(Color::fromString("#80102030") == Color::rgba(0x10, 0x20, 0x30, 0x80));
    CHECK(Color::rgb(100, 100, 100).lighter() == Color::rgb(150, 150, 150));
    CHECK(Color::rgb(100, 100, 100).darker() == Color::rgb(50, 50, 50));
    CHECK_THROWS_AS(Color::fromString("red"), std::invalid_argument);
    CHECK_THROWS_AS(Color::fromString("#12"), std::invalid_argument);
}

TEST_CASE(gui_easing_endpoints) {
    for (Easing easing : {Easing::Linear, Easing::InQuad, Easing::OutQuad,
                          Easing::InOutQuad, Easing::OutCubic, Easing::OutExpo}) {
        CHECK_EQ(applyEasing(easing, 0.0), 0.0);
        CHECK_EQ(applyEasing(easing, 1.0), 1.0);
    }
    CHECK_EQ(applyEasing(Easing::Linear, 0.5), 0.5);
    CHECK_EQ(applyEasing(Easing::InQuad, 0.5), 0.25);
    CHECK_EQ(applyEasing(Easing::OutQuad, 0.5), 0.75);
    CHECK(applyEasing(Easing::OutCubic, 0.5) > 0.8);
}

TEST_CASE(gui_animation_runs_to_target) {
    Item item;
    item.setWidth(0);
    PropertyAnimation animation(&item, "width");
    animation.setTo(100);
    animation.setDuration(std::chrono::milliseconds(40));
    animation.setEasing(Easing::Linear);

    Object context;
    bool finished = false;
    animation.connect(
        &context, PropertyAnimation::signalFinished,
        [&] {
            finished = true;
            Thread::mainThread()->quit();
        },
        ConnectionMode::Direct);
    // Watchdog so a broken animation fails instead of hanging forever.
    Timer::singleShot(std::chrono::milliseconds(5000), &context,
                      [] { Thread::mainThread()->quit(); });

    animation.start();
    CHECK(animation.running());
    Thread::mainThread()->exec();
    CHECK(finished);
    CHECK(!animation.running());
    CHECK_EQ(item.width(), 100.0f);
}

TEST_CASE(gui_animation_sequential) {
    Item item;
    item.setX(0);
    SequentialAnimation sequence;
    auto* first = new NumberAnimation(&item, "x", &sequence);
    first->setTo(50);
    first->setDuration(std::chrono::milliseconds(30));
    auto* pause = new PauseAnimation(&sequence);
    pause->setDuration(std::chrono::milliseconds(20));
    auto* second = new NumberAnimation(&item, "x", &sequence);
    second->setTo(100);
    second->setDuration(std::chrono::milliseconds(30));

    int firstFinished = 0;
    int secondFinished = 0;
    first->connect(&item, Animation::signalFinished, [&] { ++firstFinished; },
                   ConnectionMode::Direct);
    second->connect(&item, Animation::signalFinished,
                    [&] {
                        ++secondFinished;
                        CHECK_EQ(item.x(), 100.0f);
                    },
                    ConnectionMode::Direct);
    sequence.connect(&item, Animation::signalFinished,
                     [] { Thread::mainThread()->quit(); }, ConnectionMode::Direct);
    Timer::singleShot(std::chrono::milliseconds(5000), &item,
                      [] { Thread::mainThread()->quit(); });

    sequence.start();
    Thread::mainThread()->exec();
    CHECK_EQ(firstFinished, 1);
    CHECK_EQ(secondFinished, 1);
    CHECK_EQ(item.x(), 100.0f);
}

TEST_CASE(gui_animation_parallel) {
    Item item;
    item.setX(0);
    ParallelAnimation parallel;
    auto* number = new NumberAnimation(&item, "x", &parallel);
    number->setTo(50);
    number->setDuration(std::chrono::milliseconds(30));
    auto* pause = new PauseAnimation(&parallel);
    pause->setDuration(std::chrono::milliseconds(50));

    parallel.connect(&item, Animation::signalFinished,
                     [] { Thread::mainThread()->quit(); }, ConnectionMode::Direct);
    Timer::singleShot(std::chrono::milliseconds(5000), &item,
                      [] { Thread::mainThread()->quit(); });

    parallel.start();
    Thread::mainThread()->exec();
    CHECK_EQ(item.x(), 50.0f);
}

TEST_CASE(gui_behavior_animates_changes) {
    Item item;
    item.setX(0); // before the behavior: applied instantly
    auto* animation = new NumberAnimation(&item); // parented for ownership
    animation->setDuration(std::chrono::milliseconds(60));
    animation->setEasing(Easing::OutQuad);
    item.addBehavior("x", animation);
    CHECK(item.behavior("x") == animation);

    bool finished = false;
    animation->connect(
        &item, Animation::signalFinished,
        [&] {
            finished = true;
            Thread::mainThread()->quit();
        },
        ConnectionMode::Direct);
    Timer::singleShot(std::chrono::milliseconds(5000), &item,
                      [] { Thread::mainThread()->quit(); });

    item.setProperty("x", std::any(100.0));
    CHECK(animation->running()); // the write was consumed by the behavior
    CHECK(item.x() < 100.0f);

    Thread::mainThread()->exec();
    CHECK(finished);
    CHECK_EQ(item.x(), 100.0f);

    // Unrelated properties are not intercepted.
    item.setProperty("y", std::any(5.0));
    CHECK_EQ(item.y(), 5.0f);
}

TEST_CASE(gui_behavior_restart_midflight) {
    Item item;
    item.setX(0);
    auto* animation = new NumberAnimation(&item);
    animation->setDuration(std::chrono::milliseconds(200));
    item.addBehavior("x", animation);

    bool finished = false;
    animation->connect(
        &item, Animation::signalFinished,
        [&] {
            finished = true;
            Thread::mainThread()->quit();
        },
        ConnectionMode::Direct);
    Timer::singleShot(std::chrono::milliseconds(5000), &item,
                      [] { Thread::mainThread()->quit(); });

    item.setProperty("x", std::any(100.0));
    // Retarget mid-flight: the animation restarts from the current value.
    Timer::singleShot(std::chrono::milliseconds(30), &item, [&] {
        item.setProperty("x", std::any(200.0));
    });
    Thread::mainThread()->exec();
    CHECK(finished);
    CHECK_EQ(item.x(), 200.0f);
}

TEST_CASE(gui_behavior_color_animation) {
    Rectangle rect;
    rect.setColor(Color::rgb(0, 0, 0));
    auto* animation = new ColorAnimation(&rect);
    animation->setDuration(std::chrono::milliseconds(40));
    rect.addBehavior("color", animation);

    bool finished = false;
    animation->connect(
        &rect, Animation::signalFinished,
        [&] {
            finished = true;
            Thread::mainThread()->quit();
        },
        ConnectionMode::Direct);
    Timer::singleShot(std::chrono::milliseconds(5000), &rect,
                      [] { Thread::mainThread()->quit(); });

    rect.setProperty("color", std::any(Color::rgb(100, 200, 255)));
    CHECK(animation->running());
    Thread::mainThread()->exec();
    CHECK(finished);
    CHECK(rect.color() == Color::rgb(100, 200, 255));
}


TEST_CASE(gui_text_implicit_size) {
    // Content-driven while a dimension is 0; explicit sizes are kept.
    Text free;
    free.setText("AB"); // 2 glyphs x 8px at the default scale 1
    CHECK_EQ(free.width(), 16.0f);
    CHECK_EQ(free.height(), 8.0f);
    free.setFontSize(16); // scale 2
    CHECK_EQ(free.width(), 32.0f);
    CHECK_EQ(free.height(), 16.0f);

    Text fixed;
    fixed.setWidth(50);
    fixed.setText("AB");
    CHECK_EQ(fixed.width(), 50.0f);
    CHECK_EQ(fixed.height(), 8.0f); // height was unset -> content-driven

    Text multiline;
    multiline.setText("AB\nABC");
    CHECK_EQ(multiline.width(), 24.0f);
    CHECK_EQ(multiline.height(), 16.0f);
}

#ifdef FIRMIR_RENDER_TEST_SOFT
TEST_CASE(gui_painter_draws_text_glyphs) {
    auto device = Render::Render::createDevice("soft");
    CHECK(device != nullptr);
    if (!device)
        return;
    RenderResources resources(device.get());
    std::unique_ptr<Render::RenderTarget> target(
        device->createRenderTarget({32, 8, Render::TextureFormat::Rgba8, false}));
    std::unique_ptr<Render::RenderCommandBuffer> commands(
        device->createCommandBuffer());
    CHECK(target && commands);
    if (!target || !commands)
        return;
    commands->begin();
    Render::ClearValue clear; // opaque black
    clear.a = 1;
    commands->beginRenderPass(target.get(), clear);
    {
        Painter painter(resources, commands.get(), target.get());
        painter.drawText(0, 0, "A", Color::rgb(255, 255, 255));
    }
    commands->endRenderPass();
    commands->end();
    device->submit(commands.get());
    const auto* cpu = dynamic_cast<Render::CpuRenderTarget*>(target.get());
    CHECK(cpu != nullptr);
    if (!cpu)
        return;
    // font8x8 'A' = {0x0C, 0x1E, 0x33, ...}: row 0 lights columns 2-3,
    // row 2 lights columns 0-1; untouched pixels stay at the clear color.
    CHECK_EQ(cpu->pixels()[0 * 32 + 2], 0xFFFFFFFFu);
    CHECK_EQ(cpu->pixels()[0 * 32 + 3], 0xFFFFFFFFu);
    CHECK_EQ(cpu->pixels()[2 * 32 + 0], 0xFFFFFFFFu);
    CHECK_EQ(cpu->pixels()[0 * 32 + 0], 0xFF000000u);
    CHECK_EQ(cpu->pixels()[7 * 32 + 7], 0xFF000000u);
}
#endif // FIRMIR_RENDER_TEST_SOFT

TEST_CASE(gui_item_qt_extended_properties) {
    Item item;
    item.setImplicitWidth(40);
    item.setImplicitHeight(20);
    CHECK_EQ(item.width(), 40.0f);
    CHECK_EQ(item.height(), 20.0f);
    item.setWidth(70);
    item.setImplicitWidth(80); // implicit updates do not override explicit size
    CHECK_EQ(item.width(), 70.0f);
    CHECK_EQ(item.implicitWidth(), 80.0f);

    int childrenNotifications = 0;
    item.connect<std::string, std::any>(
        &item, Object::signalPropertyChanged,
        [&](const std::string& name, const std::any&) {
            if (name == "childrenRect")
                ++childrenNotifications;
        },
        ConnectionMode::Direct);
    auto* first = new Item(&item);
    first->setGeometry(-10, 5, 20, 30);
    auto* second = new Item(&item);
    second->setGeometry(15, -5, 10, 20);
    const RectF bounds = item.childrenRect();
    CHECK_EQ(bounds.x, -10.0f);
    CHECK_EQ(bounds.y, -5.0f);
    CHECK_EQ(bounds.width, 35.0f);
    CHECK_EQ(bounds.height, 40.0f);
    CHECK(childrenNotifications >= 2);

    item.setGeometry(10, 20, 100, 50);
    item.setRotation(90);
    Point center = item.mapToParent({50, 25});
    CHECK_EQ(center.x, 60.0f);
    CHECK_EQ(center.y, 45.0f);
    Point corner = item.mapToParent({0, 0});
    CHECK(corner.x > 84.9f && corner.x < 85.1f);
    CHECK(corner.y > -5.1f && corner.y < -4.9f);
    Point roundTrip = item.mapFromParent(corner);
    CHECK(std::fabs(roundTrip.x) < 0.001f);
    CHECK(std::fabs(roundTrip.y) < 0.001f);
    item.setRotation(0);
    item.setScale(2);
    center = item.mapToParent({50, 25});
    CHECK_EQ(center.x, 60.0f);
    CHECK_EQ(center.y, 45.0f);
    corner = item.mapToParent({0, 0});
    CHECK_EQ(corner.x, -40.0f);
    CHECK_EQ(corner.y, -5.0f);

    first->setZ(2);
    second->setZ(-1);
    CHECK_EQ(item.childItems().front(), second);
    CHECK_EQ(item.childItems().back(), first);
    item.setClip(true);
    CHECK(item.clip());
    item.setTransformOrigin(TransformOrigin::TopLeft);
    CHECK(item.transformOriginPoint().x == 0);
    item.setActiveFocusOnTab(true);
    CHECK(item.activeFocusOnTab());
    item.setFocusPolicy(FocusPolicy::NoFocus);
    CHECK_EQ(static_cast<int>(item.focusPolicy()), 0);
    delete first;
    delete second;
}

TEST_CASE(gui_item_active_focus_and_tab) {
    ensurePlatform();
    firmir_gui_headless_add_screen("FOCUS-EXT-1", 200, 100);
    const int windowIndex = firmir_gui_headless_window_count();
    {
        Window window(Platform::instance()->screens().back());
        window.setGeometry(Rect{0, 0, 200, 100});
        auto* first = window.createItem<Item>(window.contentItem());
        first->setGeometry(0, 0, 50, 50);
        first->setActiveFocusOnTab(true);
        auto* second = window.createItem<Item>(window.contentItem());
        second->setGeometry(60, 0, 50, 50);
        second->setActiveFocusOnTab(true);
        window.show();
        first->setFocus();
        CHECK(first->hasFocus());
        CHECK(first->hasActiveFocus());
        CHECK(!second->hasActiveFocus());
        firmir_gui_headless_inject_key(true, 15, "\t"); // KEY_TAB
        CHECK(second->hasFocus());
        CHECK(second->hasActiveFocus());
        window.hide();
        CHECK(second->hasFocus()); // local focus survives hiding
        CHECK(!second->hasActiveFocus());
        window.show();
        CHECK(second->hasActiveFocus());
    }
    (void)windowIndex;
    firmir_gui_headless_remove_screen("FOCUS-EXT-1");
}
