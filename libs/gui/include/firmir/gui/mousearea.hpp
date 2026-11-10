#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>

#include <memory>

namespace Firmir {
namespace Gui {

/// Pointer/touch input item.
class MouseArea : public Item {
public:
    explicit MouseArea(Object* parent = nullptr);
    ~MouseArea() override;

    bool isPressed() const;
    bool containsMouse() const;
    bool hoverEnabled() const;
    void setHoverEnabled(bool enabled);
    int acceptedButtons() const;
    void setAcceptedButtons(int buttons);
    float mouseX() const;
    float mouseY() const;

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

    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
