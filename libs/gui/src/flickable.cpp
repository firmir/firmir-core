#include <firmir/gui/flickable.hpp>
#include <algorithm>
#include <cmath>

namespace Firmir {
namespace Gui {

class Flickable::Impl {
public:
    explicit Impl(Flickable* q) : content(new Item(q)) {}

    Item* content;
    float contentX = 0, contentY = 0;
    float contentWidth = 0, contentHeight = 0;
    float velocityX = 0, velocityY = 0;
    Point lastPosition;
    bool moving = false, dragging = false;
    int timer = 0;
};

Flickable::Flickable(Object* parent)
    : Item(parent), d(std::make_unique<Impl>(this)) {
    setClip(true);
    registerProperty<float>("contentX", &Flickable::contentX, &Flickable::setContentX);
    registerProperty<float>("contentY", &Flickable::contentY, &Flickable::setContentY);
    registerProperty<float>("contentWidth", &Flickable::contentWidth, &Flickable::setContentWidth);
    registerProperty<float>("contentHeight", &Flickable::contentHeight, &Flickable::setContentHeight);
}
Flickable::~Flickable() = default;
Item* Flickable::contentItem() const { return d->content; }
float Flickable::contentX() const { return d->contentX; }
float Flickable::contentY() const { return d->contentY; }
float Flickable::contentWidth() const { return d->contentWidth; }
float Flickable::contentHeight() const { return d->contentHeight; }
bool Flickable::isMoving() const { return d->moving; }
bool Flickable::isDragging() const { return d->dragging; }
void Flickable::updateContentPosition() {
    auto sceneLock = lockScene();
    const float maximumX = std::max(0.0f, d->contentWidth - width());
    const float maximumY = std::max(0.0f, d->contentHeight - height());
    d->contentX = std::clamp(d->contentX, 0.0f, maximumX);
    d->contentY = std::clamp(d->contentY, 0.0f, maximumY);
    d->content->setGeometry(-d->contentX, -d->contentY, d->contentWidth, d->contentHeight);
}
void Flickable::setContentX(float value) {
    auto sceneLock = lockScene();
    const float old = d->contentX;
    d->contentX = value;
    updateContentPosition();
    if (old != d->contentX) { notifyPropertyChanged("contentX"); update(); }
}
void Flickable::setContentY(float value) {
    auto sceneLock = lockScene();
    const float old = d->contentY;
    d->contentY = value;
    updateContentPosition();
    if (old != d->contentY) { notifyPropertyChanged("contentY"); update(); }
}
void Flickable::setContentWidth(float value) {
    auto sceneLock = lockScene();
    d->contentWidth = std::max(0.0f, value);
    updateContentPosition();
    notifyPropertyChanged("contentWidth");
}
void Flickable::setContentHeight(float value) {
    auto sceneLock = lockScene();
    d->contentHeight = std::max(0.0f, value);
    updateContentPosition();
    notifyPropertyChanged("contentHeight");
}
void Flickable::setMoving(bool moving) {
    auto sceneLock = lockScene();
    if (d->moving == moving) return;
    d->moving = moving;
    emitSignal(moving ? signalMovementStarted : signalMovementEnded);
}
void Flickable::mouseEvent(MouseEvent& event) {
    if (event.action == MouseEvent::Action::Wheel) {
        // One wheel detent scrolls roughly three text lines. The event stays
        // unaccepted when the content cannot move, so it bubbles to an outer
        // Flickable (if any).
        constexpr float step = 48.0f;
        const float maximumX = std::max(0.0f, d->contentWidth - width());
        const float maximumY = std::max(0.0f, d->contentHeight - height());
        const float targetX =
            std::clamp(d->contentX + event.delta.x * step, 0.0f, maximumX);
        const float targetY =
            std::clamp(d->contentY - event.delta.y * step, 0.0f, maximumY);
        if (targetX != d->contentX || targetY != d->contentY) {
            stop();
            setContentX(targetX);
            setContentY(targetY);
            event.accepted = true;
        }
        return;
    }
    if (event.action == MouseEvent::Action::Press && event.button == 1) {
        stop();
        d->dragging = true;
        d->lastPosition = event.pos;
        setMoving(true);
        event.accepted = true;
    } else if (event.action == MouseEvent::Action::Move && d->dragging) {
        const float dx = event.pos.x - d->lastPosition.x;
        const float dy = event.pos.y - d->lastPosition.y;
        d->lastPosition = event.pos;
        setContentX(d->contentX - dx);
        setContentY(d->contentY - dy);
        d->velocityX = -dx * 60.0f;
        d->velocityY = -dy * 60.0f;
        event.accepted = true;
    } else if (event.action == MouseEvent::Action::Release && d->dragging) {
        d->dragging = false;
        event.accepted = true;
        if (std::abs(d->velocityX) > 20 || std::abs(d->velocityY) > 20)
            flick(d->velocityX, d->velocityY);
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
    d->velocityX = vx;
    d->velocityY = vy;
    if (!d->timer) d->timer = startTimer(std::chrono::milliseconds(16));
    setMoving(true);
}
void Flickable::stop() {
    auto sceneLock = lockScene();
    if (d->timer) { killTimer(d->timer); d->timer = 0; }
    d->velocityX = d->velocityY = 0;
    d->dragging = false;
    setMoving(false);
}
void Flickable::timerEvent(int timerId) {
    auto sceneLock = lockScene();
    if (timerId != d->timer) return;
    setContentX(d->contentX + d->velocityX * 0.016f);
    setContentY(d->contentY + d->velocityY * 0.016f);
    d->velocityX *= 0.92f;
    d->velocityY *= 0.92f;
    const bool atXBounds = d->contentX <= 0 || d->contentX >= std::max(0.0f, d->contentWidth - width());
    const bool atYBounds = d->contentY <= 0 || d->contentY >= std::max(0.0f, d->contentHeight - height());
    if (atXBounds) d->velocityX = 0;
    if (atYBounds) d->velocityY = 0;
    if (std::abs(d->velocityX) < 2 && std::abs(d->velocityY) < 2) stop();
}

} // namespace Gui
} // namespace Firmir
