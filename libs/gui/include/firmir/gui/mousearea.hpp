#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>

namespace Firmir {
namespace Gui {

/// Pointer/touch input item (QML MouseArea analogue).
class MouseArea : public Item {
public:
    explicit MouseArea(Object* parent = nullptr);
    ~MouseArea() override;

    bool isPressed() const { return m_pressed; }
    bool containsMouse() const { return m_containsMouse; }
    bool hoverEnabled() const { return m_hoverEnabled; }
    void setHoverEnabled(bool enabled);
    int acceptedButtons() const { return m_acceptedButtons; }
    void setAcceptedButtons(int buttons) { auto lock = lockScene(); m_acceptedButtons = buttons; }
    float mouseX() const { return m_mouse.x; }
    float mouseY() const { return m_mouse.y; }

    static constexpr auto signalClicked = "clicked";
    static constexpr auto signalPressed = "pressed";
    static constexpr auto signalReleased = "released";
    static constexpr auto signalPositionChanged = "positionChanged";
    static constexpr auto signalEntered = "entered";
    static constexpr auto signalExited = "exited";
protected:
    void mouseEvent(MouseEvent& event) override;
    void touchEvent(TouchEvent& event) override;
private:
    void setPressed(bool pressed);
    void setContainsMouse(bool contains);
    Point m_mouse;
    bool m_pressed = false;
    bool m_containsMouse = false;
    bool m_hoverEnabled = false;
    int m_acceptedButtons = 1;
};

} // namespace Gui
} // namespace Firmir
