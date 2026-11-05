#pragma once

#include <firmir/gui/item.hpp>

namespace Firmir {
namespace Gui {

/// Clipped, draggable and kinetic scrolling surface (QML Flickable analogue).
class Flickable : public Item {
public:
    explicit Flickable(Object* parent = nullptr);
    ~Flickable() override;
    Item* contentItem() const { return m_content; }
    float contentX() const { return m_contentX; }
    void setContentX(float value);
    float contentY() const { return m_contentY; }
    void setContentY(float value);
    float contentWidth() const { return m_contentWidth; }
    void setContentWidth(float value);
    float contentHeight() const { return m_contentHeight; }
    void setContentHeight(float value);
    bool isMoving() const { return m_moving; }
    bool isDragging() const { return m_dragging; }
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
    Item* m_content;
    float m_contentX = 0, m_contentY = 0;
    float m_contentWidth = 0, m_contentHeight = 0;
    float m_velocityX = 0, m_velocityY = 0;
    Point m_lastPosition;
    bool m_moving = false, m_dragging = false;
    int m_timer = 0;
};

} // namespace Gui
} // namespace Firmir
