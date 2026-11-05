#include <firmir/wayland/compositor/surface.hpp>
#include <wayland-server.h>
#include <cstring>

namespace Firmir {
namespace Wayland {
namespace Compositor {

WaylandSurface::WaylandSurface(Object* parent) : WaylandObject(parent) {}
void WaylandSurface::attach(wl_resource* buffer, int x, int y) {
    m_pendingBuffer = WaylandBufferRef(buffer);
    m_attachX = x;
    m_attachY = y;
}
void WaylandSurface::addDamage(const Gui::Rect& rect) {
    m_damage.push_back(rect);
    emitSignal(signalDamaged, rect);
}
void WaylandSurface::commit() {
    m_buffer = m_pendingBuffer;
    m_pendingBuffer = WaylandBufferRef();
    m_damage.clear();
    emitSignal(signalCommitted);
}

WaylandSurfaceGrabber::Frame WaylandSurfaceGrabber::grab(WaylandSurface* surface) const {
    Frame frame;
    if (!surface || !surface->buffer().isShm()) return frame;
    const WaylandBufferRef buffer = surface->buffer();
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

WaylandGuiItem::WaylandGuiItem(Object* parent) : Gui::Item(parent) {}
void WaylandGuiItem::paint(Gui::Painter& painter) {
    if (!m_view || !m_view->surface()) return;
    const auto frame = WaylandSurfaceGrabber().grab(m_view->surface());
    if (frame.pixels.empty()) return;
    ++m_generation;
    if (implicitWidth() != frame.width) setImplicitWidth(frame.width);
    if (implicitHeight() != frame.height) setImplicitHeight(frame.height);
    painter.drawRgba(Gui::RectF{0, 0, width() > 0 ? width() : static_cast<float>(frame.width),
                                height() > 0 ? height() : static_cast<float>(frame.height)},
                     frame.pixels.data(), frame.width, frame.height, this, m_generation);
}
WaylandGuiItem* WaylandGuiShellIntegration::createItem(WaylandView* view, Object* parent) {
    auto* item = new WaylandGuiItem(parent);
    item->setView(view);
    return item;
}

} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
