#include <firmir/gui/mousearea.hpp>

namespace Firmir {
namespace Gui {

MouseArea::MouseArea(Object* parent) : Item(parent) {
    registerPropertyImpl("pressed", typeid(bool),
        [](const Object* o) -> std::any { return static_cast<const MouseArea*>(o)->isPressed(); },
        [](Object*, const std::any&) { throw std::invalid_argument("pressed is read-only"); });
    registerPropertyImpl("containsMouse", typeid(bool),
        [](const Object* o) -> std::any { return static_cast<const MouseArea*>(o)->containsMouse(); },
        [](Object*, const std::any&) { throw std::invalid_argument("containsMouse is read-only"); });
    registerProperty<bool>("hoverEnabled", &MouseArea::hoverEnabled, &MouseArea::setHoverEnabled);
    registerProperty<int>("acceptedButtons", &MouseArea::acceptedButtons, &MouseArea::setAcceptedButtons);
    registerPropertyImpl("mouseX", typeid(float),
        [](const Object* o) -> std::any { return static_cast<const MouseArea*>(o)->mouseX(); },
        [](Object*, const std::any&) { throw std::invalid_argument("mouseX is read-only"); });
    registerPropertyImpl("mouseY", typeid(float),
        [](const Object* o) -> std::any { return static_cast<const MouseArea*>(o)->mouseY(); },
        [](Object*, const std::any&) { throw std::invalid_argument("mouseY is read-only"); });
}
MouseArea::~MouseArea() = default;
void MouseArea::setHoverEnabled(bool enabled) {
    auto sceneLock = lockScene();
    if (m_hoverEnabled == enabled) return;
    m_hoverEnabled = enabled;
    notifyPropertyChanged("hoverEnabled");
}
void MouseArea::setPressed(bool pressed) {
    auto sceneLock = lockScene();
    if (m_pressed == pressed) return;
    m_pressed = pressed;
    notifyPropertyChanged("pressed");
}
void MouseArea::setContainsMouse(bool contains) {
    auto sceneLock = lockScene();
    if (m_containsMouse == contains) return;
    m_containsMouse = contains;
    notifyPropertyChanged("containsMouse");
    emitSignal(contains ? signalEntered : signalExited);
}
void MouseArea::mouseEvent(MouseEvent& event) {
    const bool inside = RectF{0, 0, width(), height()}.contains(event.pos.x, event.pos.y);
    const bool moved = m_mouse.x != event.pos.x || m_mouse.y != event.pos.y;
    m_mouse = event.pos;
    if (moved) {
        // Firml bindings/handlers track the pointer through the properties
        // (signals carry MouseEvent/Point, which are not Firml value types).
        notifyPropertyChanged("mouseX");
        notifyPropertyChanged("mouseY");
    }
    if (event.action == MouseEvent::Action::Move) {
        if (m_hoverEnabled || m_pressed) {
            setContainsMouse(inside);
            emitSignal(signalPositionChanged, m_mouse);
            event.accepted = true;
        }
        return;
    }
    if (event.action == MouseEvent::Action::Press) {
        if (!(event.button & m_acceptedButtons)) return;
        setContainsMouse(inside);
        setPressed(true);
        event.accepted = true;
        emitSignal(signalPressed, event);
        return;
    }
    if (event.action == MouseEvent::Action::Release && m_pressed) {
        setPressed(false);
        event.accepted = true;
        emitSignal(signalReleased, event);
        if (inside) emitSignal(signalClicked, event);
    }
}
void MouseArea::touchEvent(TouchEvent& event) {
    if (event.points.empty()) return;
    MouseEvent mouse;
    mouse.action = event.action == TouchEvent::Action::Begin ? MouseEvent::Action::Press
        : event.action == TouchEvent::Action::End ? MouseEvent::Action::Release
                                                 : MouseEvent::Action::Move;
    mouse.button = 1;
    mouse.pos = event.points.front().pos;
    mouseEvent(mouse);
    event.accepted = mouse.accepted;
}

} // namespace Gui
} // namespace Firmir
