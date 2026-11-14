#include <firmir/wayland/compositor/input.hpp>
#include <firmir/wayland/compositor/surface.hpp>
#include <wayland-server.h>
#include <chrono>
#include <cstring>
#include <mutex>
#include <set>

namespace Firmir {
namespace Wayland {
namespace Compositor {
namespace {

std::uint32_t nowMs() {
    return static_cast<std::uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

/// Gui::MouseEvent button (1/2/3) -> evdev button code.
std::uint32_t linuxButton(int button) {
    switch (button) {
    case 1: return 0x110; // BTN_LEFT
    case 2: return 0x111; // BTN_RIGHT
    case 3: return 0x112; // BTN_MIDDLE
    default: return 0;
    }
}

} // namespace

class WaylandSurface::Impl {
public:
    WaylandResource resource;
    WaylandBufferRef buffer;
    WaylandBufferRef pendingBuffer;
    /// True between wl_surface.attach and the next commit; an attach(null)
    /// therefore clears the buffer while a bare commit keeps the old one.
    bool hasPendingAttach = false;
    std::unique_ptr<WaylandSurfaceRole> role;
    std::vector<Gui::Rect> damage;
    int attachX = 0, attachY = 0;
    /// Serializes the buffer swap in commit() (wayland dispatch thread)
    /// against the pixel copy in WaylandSurfaceGrabber::grab() (render
    /// thread): wl_buffer.release for a replaced buffer is only sent when no
    /// grab of it can be in flight.
    std::mutex bufferMutex;
};

class WaylandView::Impl {
public:
    WaylandSurface* surface = nullptr;
    WaylandOutput* output = nullptr;
    Gui::Point position;
};

class WaylandGuiItem::Impl {
public:
    WaylandView* view = nullptr;
    WaylandSeat* seat = nullptr;
    std::uint64_t generation = 0;
    /// wl_pointer hover state of the hosted surface.
    bool pointerInside = false;
    /// Linux button codes currently pressed on it. The client keeps the
    /// implicit grab until the last release, wherever the pointer moves.
    std::uint32_t pressedButtons = 0;
    /// Touch contacts that began on this item (window grab covers the rest).
    std::set<int> touches;
};

class WaylandGuiShellSurfaceItem::Impl {
public:
    WaylandShellSurface* shellSurface = nullptr;
};

WaylandSurface::WaylandSurface(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
WaylandSurface::~WaylandSurface() = default;
WaylandResource WaylandSurface::resource() const { return d->resource; }
void WaylandSurface::setResource(wl_resource* resource) { d->resource = WaylandResource(resource); }
WaylandBufferRef WaylandSurface::buffer() const {
    std::lock_guard<std::mutex> lock(d->bufferMutex);
    return d->buffer;
}
WaylandBufferRef WaylandSurface::pendingBuffer() const {
    std::lock_guard<std::mutex> lock(d->bufferMutex);
    return d->pendingBuffer;
}
WaylandSurfaceRole* WaylandSurface::role() const { return d->role.get(); }
void WaylandSurface::setRole(std::unique_ptr<WaylandSurfaceRole> role) { d->role = std::move(role); }
const std::vector<Gui::Rect>& WaylandSurface::damage() const { return d->damage; }
void WaylandSurface::attach(wl_resource* buffer, int x, int y) {
    std::lock_guard<std::mutex> lock(d->bufferMutex);
    d->pendingBuffer = WaylandBufferRef(buffer);
    d->hasPendingAttach = true;
    d->attachX = x;
    d->attachY = y;
}
void WaylandSurface::addDamage(const Gui::Rect& rect) {
    d->damage.push_back(rect);
    emitSignal(signalDamaged, rect);
}
void WaylandSurface::commit() {
    wl_resource* release = nullptr;
    {
        std::lock_guard<std::mutex> lock(d->bufferMutex);
        if (d->hasPendingAttach) {
            const WaylandBufferRef old = d->buffer;
            d->buffer = d->pendingBuffer;
            d->pendingBuffer = WaylandBufferRef();
            d->hasPendingAttach = false;
            // Hand the replaced buffer back to the client. The mutex
            // guarantees the render thread is not copying it right now.
            // Re-committing the same buffer keeps it (still the current
            // frame) and must NOT release it.
            if (old.valid() && old.handle() != d->buffer.handle())
                release = old.handle();
        }
    }
    d->damage.clear();
    if (release)
        wl_buffer_send_release(release);
    emitSignal(signalCommitted);
}

WaylandSurfaceGrabber::Frame WaylandSurfaceGrabber::grab(WaylandSurface* surface) const {
    Frame frame;
    if (!surface) return frame;
    // Hold the buffer across the whole copy: commit() swaps the current
    // buffer and releases the old one to the client under the same mutex, so
    // the client cannot rewrite the shm while we read it.
    std::lock_guard<std::mutex> bufferLock(surface->d->bufferMutex);
    if (!surface->d->buffer.isShm()) return frame;
    const WaylandBufferRef buffer = surface->d->buffer;
    const Size size = buffer.size();
    const int stride = buffer.stride();
    const auto* data = static_cast<const std::uint8_t*>(buffer.data());
    if (!data || size.width <= 0 || size.height <= 0 || stride < size.width * 4)
        return frame;
    frame.width = size.width;
    frame.height = size.height;
    frame.stride = stride;
    frame.pixels.resize(static_cast<std::size_t>(size.width) * size.height);
    wl_shm_buffer* shm = wl_shm_buffer_get(buffer.handle());
    wl_shm_buffer_begin_access(shm);
    for (int y = 0; y < size.height; ++y)
        std::memcpy(frame.pixels.data() + static_cast<std::size_t>(y) * size.width,
                    data + static_cast<std::size_t>(y) * stride,
                    static_cast<std::size_t>(size.width) * 4);
    wl_shm_buffer_end_access(shm);
    return frame;
}

WaylandGuiItem::WaylandGuiItem(Object* parent)
    : Gui::Item(parent), d(std::make_unique<Impl>()) {
    // A hosted surface is a window: it takes keyboard focus on a click so
    // keyEvent() can forward the keys to its client.
    setFocusPolicy(Gui::FocusPolicy::ClickFocus);
}
WaylandGuiItem::~WaylandGuiItem() = default;
WaylandView* WaylandGuiItem::view() const { return d->view; }
void WaylandGuiItem::setView(WaylandView* view) { d->view = view; update(); }
WaylandSeat* WaylandGuiItem::seat() const { return d->seat; }
void WaylandGuiItem::setSeat(WaylandSeat* seat) { d->seat = seat; }
void WaylandGuiItem::detachSurface() {
    // Hold the scene lock across the swap: the render thread dereferences
    // the surface while painting, and the caller may be destroying it.
    auto lock = lockScene();
    if (d->view) d->view->setSurface(nullptr);
    d->pointerInside = false;
    d->pressedButtons = 0;
    d->touches.clear();
}
void WaylandGuiItem::mouseEvent(Gui::MouseEvent& event) {
    WaylandSurface* target = d->view ? d->view->surface() : nullptr;
    if (!d->seat || !target) {
        event.accepted = true;
        return;
    }
    // wl_pointer coordinates are surface-local; the item may stretch the
    // buffer when maximized/fullscreen, so scale accordingly.
    const Size bufferSize = target->buffer().size();
    float sx = event.pos.x;
    float sy = event.pos.y;
    const float itemWidth = width();
    const float itemHeight = height();
    if (bufferSize.width > 0 && bufferSize.height > 0 && itemWidth > 0 && itemHeight > 0) {
        sx *= static_cast<float>(bufferSize.width) / itemWidth;
        sy *= static_cast<float>(bufferSize.height) / itemHeight;
    }
    const float surfaceWidth = bufferSize.width > 0 ? static_cast<float>(bufferSize.width)
                                                    : itemWidth;
    const float surfaceHeight = bufferSize.height > 0 ? static_cast<float>(bufferSize.height)
                                                      : itemHeight;
    const bool inside = sx >= 0 && sy >= 0 && sx < surfaceWidth && sy < surfaceHeight;
    switch (event.action) {
    case Gui::MouseEvent::Action::Move:
        // While a button is held the client keeps the pointer grab: it must
        // keep receiving motion (possibly outside coordinates) and the leave
        // is deferred until the last button is released.
        if (d->pointerInside && (inside || d->pressedButtons)) {
            d->seat->pointer()->motion(nowMs(), sx, sy);
        } else if (inside) {
            d->seat->pointer()->enter(target, sx, sy);
            d->pointerInside = true;
        } else if (d->pointerInside) {
            d->seat->pointer()->leave(target);
            d->pointerInside = false;
        }
        break;
    case Gui::MouseEvent::Action::Press: {
        const std::uint32_t code = linuxButton(event.button);
        if (inside && !d->pointerInside) {
            d->seat->pointer()->enter(target, sx, sy);
            d->pointerInside = true;
        }
        emitSignal(signalActivated);
        if (d->pointerInside && code) {
            d->seat->pointer()->button(nowMs(), code, true);
            d->pressedButtons |= code;
        }
        break;
    }
    case Gui::MouseEvent::Action::Release: {
        const std::uint32_t code = linuxButton(event.button);
        // The release always goes to the grabbed client, even when the
        // pointer has already left the surface.
        if (d->pointerInside || (d->pressedButtons & code)) {
            d->seat->pointer()->button(nowMs(), code, false);
            d->pressedButtons &= ~code;
        }
        // Grab over: re-evaluate the hover state now.
        if (!d->pressedButtons && d->pointerInside && !inside) {
            d->seat->pointer()->leave(target);
            d->pointerInside = false;
        }
        break;
    }
    case Gui::MouseEvent::Action::Wheel:
        if (d->pointerInside && event.delta.y != 0)
            d->seat->pointer()->axis(nowMs(), 0 /*vertical*/, -event.delta.y * 10.0f);
        if (d->pointerInside && event.delta.x != 0)
            d->seat->pointer()->axis(nowMs(), 1 /*horizontal*/, -event.delta.x * 10.0f);
        break;
    }
    event.accepted = true;
}
void WaylandGuiItem::keyEvent(Gui::KeyEvent& event) {
    // KeyEvent::key is an evdev code, which is exactly what wl_keyboard puts
    // on the wire.
    if (d->seat)
        d->seat->keyboard()->key(nowMs(), static_cast<std::uint32_t>(event.key),
                                 event.pressed);
    event.accepted = true;
}
void WaylandGuiItem::focusInEvent() { emitSignal(signalActivated); }
void WaylandGuiItem::touchEvent(Gui::TouchEvent& event) {
    WaylandSurface* target = d->view ? d->view->surface() : nullptr;
    if (!d->seat || !target || event.points.empty()) {
        event.accepted = true;
        return;
    }
    // Same item -> surface scaling as the pointer.
    const Size bufferSize = target->buffer().size();
    const float itemWidth = width();
    const float itemHeight = height();
    for (const Gui::TouchPoint& point : event.points) {
        Gui::Point surfacePoint = point.pos;
        if (bufferSize.width > 0 && bufferSize.height > 0 && itemWidth > 0 &&
            itemHeight > 0) {
            surfacePoint.x *= static_cast<float>(bufferSize.width) / itemWidth;
            surfacePoint.y *= static_cast<float>(bufferSize.height) / itemHeight;
        }
        switch (event.action) {
        case Gui::TouchEvent::Action::Begin:
            if (d->touches.insert(point.id).second)
                d->seat->touch()->down(target, nowMs(), point.id, surfacePoint.x,
                                       surfacePoint.y);
            break;
        case Gui::TouchEvent::Action::Update:
            if (d->touches.count(point.id))
                d->seat->touch()->motion(nowMs(), point.id, surfacePoint.x,
                                         surfacePoint.y);
            break;
        case Gui::TouchEvent::Action::End:
            if (d->touches.erase(point.id))
                d->seat->touch()->up(nowMs(), point.id);
            break;
        }
    }
    event.accepted = true;
}

WaylandView::WaylandView(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
WaylandView::~WaylandView() = default;
WaylandSurface* WaylandView::surface() const { return d->surface; }
void WaylandView::setSurface(WaylandSurface* surface) { d->surface = surface; }
WaylandOutput* WaylandView::output() const { return d->output; }
void WaylandView::setOutput(WaylandOutput* output) { d->output = output; }
Gui::Point WaylandView::position() const { return d->position; }
void WaylandView::setPosition(Gui::Point position) { d->position = position; }

WaylandGuiShellSurfaceItem::WaylandGuiShellSurfaceItem(Object* parent)
    : WaylandGuiItem(parent), d(std::make_unique<Impl>()) {}
WaylandGuiShellSurfaceItem::~WaylandGuiShellSurfaceItem() = default;
WaylandShellSurface* WaylandGuiShellSurfaceItem::shellSurface() const { return d->shellSurface; }
void WaylandGuiShellSurfaceItem::setShellSurface(WaylandShellSurface* surface) {
    d->shellSurface = surface;
}
void WaylandGuiItem::paint(Gui::Painter& painter) {
    if (!d->view || !d->view->surface()) return;
    const auto frame = WaylandSurfaceGrabber().grab(d->view->surface());
    if (frame.pixels.empty()) return;
    ++d->generation;
    if (implicitWidth() != frame.width) setImplicitWidth(frame.width);
    if (implicitHeight() != frame.height) setImplicitHeight(frame.height);
    painter.drawRgba(Gui::RectF{0, 0, width() > 0 ? width() : static_cast<float>(frame.width),
                                height() > 0 ? height() : static_cast<float>(frame.height)},
                     frame.pixels.data(), frame.width, frame.height, this, d->generation);
}
WaylandGuiItem* WaylandGuiShellIntegration::createItem(WaylandView* view, Object* parent) {
    auto* item = new WaylandGuiItem(parent);
    item->setView(view);
    return item;
}

} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
