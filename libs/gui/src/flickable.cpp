#include <firmir/gui/flickable.hpp>
#include <algorithm>
#include <cmath>

namespace Firmir {
namespace Gui {

Flickable::Flickable(Object* parent) : Item(parent), m_content(new Item(this)) {
    setClip(true);
    registerProperty<float>("contentX", &Flickable::contentX, &Flickable::setContentX);
    registerProperty<float>("contentY", &Flickable::contentY, &Flickable::setContentY);
    registerProperty<float>("contentWidth", &Flickable::contentWidth, &Flickable::setContentWidth);
    registerProperty<float>("contentHeight", &Flickable::contentHeight, &Flickable::setContentHeight);
}
Flickable::~Flickable() = default;
void Flickable::updateContentPosition() {
    auto sceneLock = lockScene();
    const float maximumX = std::max(0.0f, m_contentWidth - width());
    const float maximumY = std::max(0.0f, m_contentHeight - height());
    m_contentX = std::clamp(m_contentX, 0.0f, maximumX);
    m_contentY = std::clamp(m_contentY, 0.0f, maximumY);
    m_content->setGeometry(-m_contentX, -m_contentY, m_contentWidth, m_contentHeight);
}
void Flickable::setContentX(float value) {
    auto sceneLock = lockScene();
    const float old = m_contentX;
    m_contentX = value;
    updateContentPosition();
    if (old != m_contentX) { notifyPropertyChanged("contentX"); update(); }
}
void Flickable::setContentY(float value) {
    auto sceneLock = lockScene();
    const float old = m_contentY;
    m_contentY = value;
    updateContentPosition();
    if (old != m_contentY) { notifyPropertyChanged("contentY"); update(); }
}
void Flickable::setContentWidth(float value) {
    auto sceneLock = lockScene();
    m_contentWidth = std::max(0.0f, value);
    updateContentPosition();
    notifyPropertyChanged("contentWidth");
}
void Flickable::setContentHeight(float value) {
    auto sceneLock = lockScene();
    m_contentHeight = std::max(0.0f, value);
    updateContentPosition();
    notifyPropertyChanged("contentHeight");
}
void Flickable::setMoving(bool moving) {
    auto sceneLock = lockScene();
    if (m_moving == moving) return;
    m_moving = moving;
    emitSignal(moving ? signalMovementStarted : signalMovementEnded);
}
void Flickable::mouseEvent(MouseEvent& event) {
    if (event.action == MouseEvent::Action::Press && event.button == 1) {
        stop();
        m_dragging = true;
        m_lastPosition = event.pos;
        setMoving(true);
        event.accepted = true;
    } else if (event.action == MouseEvent::Action::Move && m_dragging) {
        const float dx = event.pos.x - m_lastPosition.x;
        const float dy = event.pos.y - m_lastPosition.y;
        m_lastPosition = event.pos;
        setContentX(m_contentX - dx);
        setContentY(m_contentY - dy);
        m_velocityX = -dx * 60.0f;
        m_velocityY = -dy * 60.0f;
        event.accepted = true;
    } else if (event.action == MouseEvent::Action::Release && m_dragging) {
        m_dragging = false;
        event.accepted = true;
        if (std::abs(m_velocityX) > 20 || std::abs(m_velocityY) > 20)
            flick(m_velocityX, m_velocityY);
        else
            setMoving(false);
    }
}
void Flickable::touchEvent(TouchEvent& event) {
    if (event.points.empty()) return;
    MouseEvent mouse;
    mouse.pos = event.points.front().pos;
    mouse.button = 1;
    mouse.action = event.action == TouchEvent::Action::Begin ? MouseEvent::Action::Press
        : event.action == TouchEvent::Action::End ? MouseEvent::Action::Release : MouseEvent::Action::Move;
    mouseEvent(mouse);
    event.accepted = mouse.accepted;
}
void Flickable::flick(float vx, float vy) {
    auto sceneLock = lockScene();
    m_velocityX = vx;
    m_velocityY = vy;
    if (!m_timer) m_timer = startTimer(std::chrono::milliseconds(16));
    setMoving(true);
}
void Flickable::stop() {
    auto sceneLock = lockScene();
    if (m_timer) { killTimer(m_timer); m_timer = 0; }
    m_velocityX = m_velocityY = 0;
    m_dragging = false;
    setMoving(false);
}
void Flickable::timerEvent(int timerId) {
    auto sceneLock = lockScene();
    if (timerId != m_timer) return;
    setContentX(m_contentX + m_velocityX * 0.016f);
    setContentY(m_contentY + m_velocityY * 0.016f);
    m_velocityX *= 0.92f;
    m_velocityY *= 0.92f;
    const bool atXBounds = m_contentX <= 0 || m_contentX >= std::max(0.0f, m_contentWidth - width());
    const bool atYBounds = m_contentY <= 0 || m_contentY >= std::max(0.0f, m_contentHeight - height());
    if (atXBounds) m_velocityX = 0;
    if (atYBounds) m_velocityY = 0;
    if (std::abs(m_velocityX) < 2 && std::abs(m_velocityY) < 2) stop();
}

} // namespace Gui
} // namespace Firmir
