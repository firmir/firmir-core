#pragma once

#include <firmir/gui/item.hpp>

#include <memory>

namespace Firmir {
namespace Gui {

/// Clipped, draggable and kinetic scrolling surface.
class Flickable : public Item {
public:
    explicit Flickable(Object* parent = nullptr);
    ~Flickable() override;
    Item* contentItem() const;
    float contentX() const;
    void setContentX(float value);
    float contentY() const;
    void setContentY(float value);
    float contentWidth() const;
    void setContentWidth(float value);
    float contentHeight() const;
    void setContentHeight(float value);
    bool isMoving() const;
    bool isDragging() const;
    void flick(float velocityX, float velocityY);
    void stop();
    static constexpr auto signalMovementStarted = "movementStarted";
    static constexpr auto signalMovementEnded = "movementEnded";
protected:
    void mouseEvent(MouseEvent& event) override;
    void touchEvent(TouchEvent& event) override;
    void timerEvent(int timerId) override;
private:
    void updateContentPosition();
    void setMoving(bool moving);

    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
