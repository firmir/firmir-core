#include <firmir/wayland/compositor/surface.hpp>
#include <wayland-server.h>
#include <cstring>
#include <mutex>

namespace Firmir {
namespace Wayland {
namespace Compositor {

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
    std::uint64_t generation = 0;
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
    : Gui::Item(parent), d(std::make_unique<Impl>()) {}
WaylandGuiItem::~WaylandGuiItem() = default;
WaylandView* WaylandGuiItem::view() const { return d->view; }
void WaylandGuiItem::setView(WaylandView* view) { d->view = view; update(); }

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
