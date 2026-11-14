#include <firmir/wayland/compositor/bufferbackend.hpp>
#include <firmir/wayland/compositor/input.hpp>
#include <firmir/wayland/compositor/surface.hpp>
#include <wayland-server.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
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

/// Client buffer tracing, off unless FIRMIR_WAYLAND_DEBUG is set. A window
/// that shows up blank has no log line anywhere else: the client thinks it
/// committed a frame, the compositor silently reads nothing out of the buffer.
bool debugEnabled() {
    static const bool enabled = std::getenv("FIRMIR_WAYLAND_DEBUG") != nullptr;
    return enabled;
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
    /// See commitSerial().
    std::atomic<std::uint64_t> commitSerial{0};
    /// Outputs the surface already entered (wl_surface.enter is sent once).
    std::set<WaylandOutput*> entered;
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
    /// wl_pointer hover state of the hosted surface. The enter belongs to the
    /// surface, not to the item: a client that replaces its surface (a second
    /// toplevel, a re-created window) needs a fresh enter for it.
    bool pointerInside = false;
    WaylandSurface* hoverSurface = nullptr;
    /// Linux button codes currently pressed on it. The client keeps the
    /// implicit grab until the last release, wherever the pointer moves.
    std::uint32_t pressedButtons = 0;
    /// Touch contacts that began on this item (window grab covers the rest).
    std::set<int> touches;
    /// Last frame copied out of the surface (see paint()). Grabbing on every
    /// compositor frame would copy every visible client buffer at the display
    /// rate — for a fullscreen 1200x2000 window that is ~10 MB per frame — and
    /// re-upload it, holding the scene lock long enough to starve the GUI
    /// thread (input stops being dispatched while a window is dragged or
    /// animated).
    WaylandSurfaceGrabber::Frame frame;
    std::uint64_t frameSerial = 0;
    WaylandSurface* frameSurface = nullptr;
    /// A blank window is reported once: repeating the same line every frame
    /// would hide the rest of the log.
    bool reportedEmptyFrame = false;
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
            // The pixels changed (a client may re-attach the very same shm
            // buffer after drawing into it, which is why the serial is bumped
            // for every attach, not only for a new handle).
            d->commitSerial.fetch_add(1, std::memory_order_release);
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
    if (debugEnabled()) {
        wl_resource* handle = d->buffer.handle();
        if (wl_shm_buffer* shm = handle ? wl_shm_buffer_get(handle) : nullptr)
            std::fprintf(stderr,
                         "firmir-wayland-surface: commit wl_shm %dx%d stride %d fmt 0x%08x"
                         " serial %llu\n",
                         wl_shm_buffer_get_width(shm), wl_shm_buffer_get_height(shm),
                         wl_shm_buffer_get_stride(shm), wl_shm_buffer_get_format(shm),
                         static_cast<unsigned long long>(d->commitSerial.load()));
        else if (handle)
            std::fprintf(stderr,
                         "firmir-wayland-surface: commit non-wl_shm buffer %p (dma-buf / "
                         "wl_drm), read through the buffer backend\n",
                         static_cast<void*>(handle));
        else
            std::fprintf(stderr, "firmir-wayland-surface: commit without buffer\n");
    }
    if (d->buffer.valid())
        announceOutputs();
    emitSignal(signalCommitted);
}

void WaylandSurface::announceOutputs() {
    if (!d->resource.handle()) return;
    auto* compositor = static_cast<WaylandCompositor*>(parent());
    if (!compositor) return;
    for (WaylandOutput* output : compositor->outputs()) {
        if (!output || !d->entered.insert(output).second) continue;
        output->sendEnter(d->resource.handle());
        if (debugEnabled())
            std::fprintf(stderr, "firmir-wayland-surface: enter wl_output '%s'\n",
                         output->model().c_str());
    }
}

std::uint64_t WaylandSurface::commitSerial() const {
    return d->commitSerial.load(std::memory_order_acquire);
}

WaylandSurfaceGrabber::Frame WaylandSurfaceGrabber::grab(WaylandSurface* surface) const {
    Frame frame;
    if (!surface) return frame;
    // Hold the buffer across the whole copy: commit() swaps the current
    // buffer and releases the old one to the client under the same mutex, so
    // the client cannot rewrite the shm while we read it.
    std::lock_guard<std::mutex> bufferLock(surface->d->bufferMutex);
    const WaylandBufferRef buffer = surface->d->buffer;
    if (!buffer.valid()) {
        if (debugEnabled())
            std::fprintf(stderr, "firmir-wayland-surface: grab: no buffer committed yet\n");
        return frame;
    }
    // A backend plugin reads the buffer (an EGL import for GPU clients, a
    // mapping for wl_shm). Without one, wl_shm buffers are read here so the
    // library still works in a tree that has no plugins installed.
    if (BufferBackend* backend = BufferBackends::selected()) {
        std::unique_ptr<ImportedBuffer> imported = backend->import(buffer);
        if (!imported || !imported->valid()) {
            if (debugEnabled())
                std::fprintf(stderr,
                             "firmir-wayland-surface: grab: backend '%s' could not read the "
                             "buffer%s\n",
                             backend->name(), buffer.isShm() ? "" : " (non-wl_shm)");
            return frame;
        }
        frame.width = imported->width();
        frame.height = imported->height();
        frame.stride = frame.width * 4;
        // Move the frame out: the import already paid for the copy.
        frame.pixels = std::move(imported->pixels());
        if (static_cast<int>(frame.pixels.size()) != frame.width * frame.height) {
            if (debugEnabled())
                std::fprintf(stderr,
                             "firmir-wayland-surface: grab: backend '%s' returned %zu pixels "
                             "for %dx%d\n",
                             backend->name(), frame.pixels.size(), frame.width, frame.height);
            return Frame{};
        }
        return frame;
    }
    if (!buffer.isShm()) {
        if (debugEnabled())
            std::fprintf(stderr,
                         "firmir-wayland-surface: grab: no backend plugin and the buffer is "
                         "not wl_shm (dma-buf / wl_drm); nothing to draw\n");
        return frame;
    }
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
    // The cached frame belongs to the surface that is going away.
    d->frame = WaylandSurfaceGrabber::Frame();
    d->frameSerial = 0;
    d->frameSurface = nullptr;
    d->pointerInside = false;
    d->hoverSurface = nullptr;
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
    if (debugEnabled() &&
        (event.action == Gui::MouseEvent::Action::Press ||
         event.action == Gui::MouseEvent::Action::Release)) {
        std::fprintf(stderr,
                     "firmir-wayland-surface: pointer %s button=%d at (%.1f,%.1f) "
                     "surface %dx%d inside=%d hover=%d held=0x%x\n",
                     event.action == Gui::MouseEvent::Action::Press ? "press" : "release",
                     event.button, sx, sy, bufferSize.width, bufferSize.height, inside,
                     d->pointerInside, d->pressedButtons);
    }
    // The hover state belongs to the surface, not to the item: when the client
    // hands over a different surface (a second toplevel, a re-created window)
    // the old enter is useless to it, and the client would drop every event
    // that follows because it never saw an enter for its surface.
    const auto ensureEnter = [&] {
        if (!inside) return;
        if (d->pointerInside && d->hoverSurface == target) return;
        if (d->pointerInside && d->hoverSurface)
            d->seat->pointer()->leave(d->hoverSurface);
        d->seat->pointer()->enter(target, sx, sy);
        d->pointerInside = true;
        d->hoverSurface = target;
    };
    const auto hoverLeave = [&] {
        if (!d->pointerInside) return;
        d->seat->pointer()->leave(d->hoverSurface ? d->hoverSurface : target);
        d->pointerInside = false;
        d->hoverSurface = nullptr;
    };
    switch (event.action) {
    case Gui::MouseEvent::Action::Move:
        // While a button is held the client keeps the pointer grab: it must
        // keep receiving motion (possibly outside coordinates) and the leave
        // is deferred until the last button is released.
        if (d->pointerInside && d->hoverSurface == target && (inside || d->pressedButtons)) {
            d->seat->pointer()->motion(nowMs(), sx, sy);
        } else if (inside) {
            ensureEnter();
        } else {
            hoverLeave();
        }
        break;
    case Gui::MouseEvent::Action::Press: {
        const std::uint32_t code = linuxButton(event.button);
        ensureEnter();
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
        if (!d->pressedButtons && !inside)
            hoverLeave();
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
    WaylandSurface* surface = d->view->surface();
    // Copy the client's buffer only when it committed something new; the frame
    // (and the texture the painter caches for this generation) is reused
    // otherwise.
    const std::uint64_t serial = surface->commitSerial();
    // Nothing was ever committed: the window is being created, not blank.
    if (serial == 0) return;
    if (d->frameSurface != surface || serial != d->frameSerial) {
        WaylandSurfaceGrabber::Frame frame =
            WaylandSurfaceGrabber().grab(surface);
        if (frame.pixels.empty()) {
            if (!d->reportedEmptyFrame) {
                d->reportedEmptyFrame = true;
                const WaylandBufferRef buffer = surface->buffer();
                std::fprintf(stderr,
                             "firmir-wayland-item: commit %llu produced no pixels (buffer: %s); "
                             "the window stays empty. Set FIRMIR_WAYLAND_DEBUG=1 for the "
                             "per-buffer trace.\n",
                             static_cast<unsigned long long>(serial),
                             !buffer.valid() ? "none"
                             : buffer.isShm() ? "wl_shm"
                                              : "not wl_shm (dma-buf / wl_drm), needs a backend "
                                                "plugin that can read it");
            }
            // Remember the attempt: the surface will not change until the
            // client commits again.
            d->frameSerial = serial;
            d->frameSurface = surface;
            return;
        }
        d->reportedEmptyFrame = false;
        d->frame = std::move(frame);
        d->frameSerial = serial;
        d->frameSurface = surface;
        ++d->generation;
    }
    const WaylandSurfaceGrabber::Frame& frame = d->frame;
    if (frame.pixels.empty()) return;
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
