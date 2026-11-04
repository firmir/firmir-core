// firmir-gui example: a window with an animated item tree on the first
// screen of the selected backend.
//
// Backend selection:
//   FIRMIR_GUI_DISPLAY=drm|headless   display backend plugin
//   FIRMIR_GUI_INPUT=evdev|headless   input backend plugin
//   FIRMIR_GUI_PLUGIN_PATH=...        plugin search path (colon-separated)

#include <cmath>
#include <cstdio>
#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/log.hpp>
#include <firmir/thread.hpp>

using namespace Firmir;
using namespace Firmir::Gui;

namespace {

/// A plain colored rectangle item that accepts mouse/touch input.
class RectItem : public Item {
public:
    RectItem(Object* parent, Color color) : Item(parent), m_color(color) {}

    void setColor(Color color) {
        m_color = color;
        update();
    }

protected:
    void paint(Painter& painter) override {
        painter.fillRect(RectF{0, 0, width(), height()}, m_color);
    }

    void mouseEvent(MouseEvent& event) override {
        if (event.action == MouseEvent::Action::Press) {
            logInfo() << "clicked at" << event.pos.x << event.pos.y;
            event.accepted = true;
        }
    }

    void keyEvent(KeyEvent& event) override {
        if (event.pressed) {
            logInfo() << "key" << event.key << event.text;
            event.accepted = true;
        }
    }

private:
    Color m_color;
};

} // namespace

int main() {
    Platform* platform = Platform::instance();
    if (platform->screens().empty()) {
        std::fprintf(stderr,
                     "no screens — check FIRMIR_GUI_DISPLAY / "
                     "FIRMIR_GUI_PLUGIN_PATH\n");
        return 1;
    }
    Screen* screen = platform->screens().front();
    logInfo() << "screen:" << screen->name() << screen->size().width
              << screen->size().height;
    platform->connect<Screen*>(Platform::signalScreenAdded, [](Screen* s) {
        logInfo() << "screen added:" << s->name();
    });
    platform->connect<Screen*>(Platform::signalScreenRemoved, [](Screen* s) {
        logInfo() << "screen removed:" << s->name();
    });

    Window window(screen);
    window.setTitle("firmir-gui example");
    const Size size = screen->size();
    window.setGeometry(Rect{0, 0, size.width, size.height});

    auto* background = window.createItem<RectItem>(window.contentItem(),
                                                   Color::rgb(30, 30, 40));
    background->setGeometry(0, 0, size.width, size.height);

    auto* box = background->createChild<RectItem>(Color::rgb(220, 80, 60));
    box->setGeometry(100, 100, 120, 120);
    box->setOpacity(0.9f);

    auto* child = box->createChild<RectItem>(Color::rgb(80, 160, 255));
    child->setGeometry(30, 30, 60, 60);

    window.show();

    // Animate the box on the GUI thread; rendering happens on the window's
    // render thread.
    float angle = 0;
    struct Animator : public Object {
        RectItem* box;
        float* angle;
        explicit Animator(RectItem* box, float* angle)
            : box(box), angle(angle) {}
        void timerEvent(int) override {
            *angle += 0.03f;
            box->setX(200 + 150 * std::cos(*angle));
            box->setY(150 + 100 * std::sin(*angle));
        }
    };
    Animator animator(box, &angle);
    animator.startTimer(std::chrono::milliseconds(16));

    return Thread::mainThread()->exec();
}
