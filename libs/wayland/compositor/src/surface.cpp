#include <firmir/render/device.hpp>
#include <firmir/wayland/compositor/bufferbackend.hpp>
#include <firmir/wayland/compositor/input.hpp>
#include <firmir/wayland/compositor/surface.hpp>
#include <wayland-server.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <vector>

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

/// Client frames and the scene do not spell a pixel the same way: a wl_shm
/// ARGB8888 buffer (and every buffer backend, see ImportedBuffer) hands out
/// 0xAARRGGBB values, while Painter::drawRgba() takes R,G,B,A bytes — the
/// layout of a decoded PNG. Without the swap here red and blue change places
/// and every window of a client is drawn with a warm cast.
void argbToRgba(std::vector<std::uint32_t>& pixels) {
    for (std::uint32_t& pixel : pixels) {
        pixel = (pixel & 0xff00ff00u) | ((pixel & 0x00ff0000u) >> 16) |
                ((pixel & 0x000000ffu) << 16);
    }
}


/**
 * @brief FIRMIR_WAYLAND_DUMP: a directory that receives one PPM per client
 *        surface, holding the first frame the compositor took out of it.
 *
 * A window that looks wrong raises exactly one question: did the compositor
 * receive the client's colours or did it produce them? A PPM is unambiguous
 * (R, G, B bytes), so it can be compared with what the screen shows. Setting
 * the variable costs one copy of one frame per surface.
 */
/// How many of a window's client buffers keep their import. A swapchain holds
/// two to four buffers; more than that belong to a client that is not cycling
/// them, and the memory is worth more than the reuse.
constexpr std::size_t kImportedBuffers = 4;

bool dumpEnabled() {
    const char* directory = std::getenv("FIRMIR_WAYLAND_DUMP");
    return directory && *directory;
}

void dumpFrame(const WaylandBufferRef& buffer,
               const WaylandSurfaceGrabber::Frame& frame) {
    const char* directory = std::getenv("FIRMIR_WAYLAND_DUMP");
    if (!directory || !*directory || frame.width <= 0 || frame.height <= 0 ||
        frame.pixels.empty())
        return;
    static std::atomic<int> counter{0};
    char path[512];
    std::snprintf(path, sizeof(path), "%s/surface-%d.ppm", directory, counter++);
    std::FILE* file = std::fopen(path, "wb");
    if (!file) {
        std::fprintf(stderr, "firmir-wayland-surface: cannot write %s\n", path);
        return;
    }
    std::fprintf(file, "P6\n%d %d\n255\n", frame.width, frame.height);
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(frame.width) *
                                  static_cast<std::size_t>(frame.height) * 3);
    for (std::size_t i = 0; i < frame.pixels.size(); ++i) {
        const std::uint8_t* rgba =
            reinterpret_cast<const std::uint8_t*>(&frame.pixels[i]);
        rgb[i * 3] = rgba[0];
        rgb[i * 3 + 1] = rgba[1];
        rgb[i * 3 + 2] = rgba[2];
    }
    std::fwrite(rgb.data(), 1, rgb.size(), file);
    std::fclose(file);
    std::fprintf(stderr,
                 "firmir-wayland-surface: wrote %s (%dx%d): the frame as the compositor "
                 "received it, buffer %s\n",
                 path, frame.width, frame.height,
                 buffer.isShm() ? "wl_shm" : "dma-buf (wl_drm)");
}

/**
 * @brief Process-wide default of WaylandGuiItem::cpuFallbackEnabled().
 *
 * A copy per commit is the right default: a compositor that cannot sample the
 * client's buffer still shows something. An embedding compositor that runs on a
 * device where the import never works can turn it off (firmir-compositor2 keeps
 * it on for exactly that reason), and FIRMIR_WAYLAND_CPU_FALLBACK overrides
 * both ways — "0" off, anything else on — the escape hatch for an on-site check
 * without a rebuild.
 */
bool& cpuFallbackDefault() {
    static bool enabled = true;
    return enabled;
}

bool cpuFallbackPolicy() {
    // Read on every use: the variable is a debugging switch, and a paint is
    // nowhere near cheap enough for one getenv() to matter.
    const char* value = std::getenv("FIRMIR_WAYLAND_CPU_FALLBACK");
    if (value)
        return std::strcmp(value, "0") != 0;
    return cpuFallbackDefault();
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
    /// animated). The pixels only exist while there is no render device to
    /// upload them to: otherwise the copy lives in `copied` below and the frame
    /// itself is dropped as soon as it has been uploaded.
    WaylandSurfaceGrabber::Frame frame;
    /// The copied frame as this item's own texture. Holding it here (instead of
    /// letting the painter cache the pixels) is what lets the frame's pixels go:
    /// a window would otherwise keep the frame and a texture of it, ~10 MB each
    /// for a fullscreen client, and a compositor hosting a dozen windows pays it
    /// a dozen times.
    std::unique_ptr<Render::RenderTexture> copied;
    /// Device `copied` belongs to (a texture of another device cannot be used).
    Render::RenderDevice* copyDevice = nullptr;
    /// Size of `copied`, which outlives the pixels it was uploaded from.
    int copyWidth = 0;
    int copyHeight = 0;
    std::uint64_t frameSerial = 0;
    WaylandSurface* frameSurface = nullptr;
    /// Zero-copy imports of the client's buffers, one per wl_buffer the window
    /// has shown (see paint()). Keeping them is what makes the GPU path cheap:
    /// a client draws into a small set of buffers in rotation, so the texture
    /// (and its EGLImage) is created once per buffer instead of once per
    /// commit, and a swapchain that comes around again finds its texture. An
    /// entry with no texture remembers a buffer the import failed for: the
    /// attempt is repeated when the buffer is committed again, not on every
    /// painted frame. The CPU frame above is only filled for wl_shm clients
    /// and — when cpuFallbackEnabled() allows it — for the dma-bufs the import
    /// failed for.
    struct ImportedBuffer {
        WaylandBufferRef buffer;
        std::unique_ptr<Render::RenderTexture> texture;
        std::uint64_t lastUsed = 0;
        /// Commit the import was last attempted for (see paint()).
        std::uint64_t triedSerial = 0;
    };
    std::vector<ImportedBuffer> imports;
    /// Device the imports above were made on: a texture of a different device
    /// cannot be sampled by this one.
    Render::RenderDevice* importDevice = nullptr;
    std::uint64_t importClock = 0;
    /// FIRMIR_WAYLAND_DUMP wrote this surface's first frame already.
    bool dumped = false;
    /// Picture clipping radius in pixels (see setCornerRadius()).
    float cornerRadius = 0.0f;
    /// Whether a frame the zero-copy path could not deliver is copied through
    /// the CPU (see cpuFallbackEnabled()), and whether that is this item's own
    /// decision or the process default's.
    bool cpuFallback = true;
    bool cpuFallbackSet = false;
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

void WaylandSurface::forgetOutput(WaylandOutput* output) {
    d->entered.erase(output);
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
    // The client may destroy the buffer while it is being read — and take the
    // memory of a wl_shm buffer with it. The guard makes the destruction wait
    // for the copy below (see WaylandBufferRef::readLock()).
    const std::shared_lock<std::shared_mutex> readable = buffer.readLock();
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
        // A backend that read its pixels as GL_RGBA hands them over in the
        // painter's order already; swapping them here would cost a pass over the
        // whole frame and undo the read.
        if (!imported->pixelsAreRgba())
            argbToRgba(frame.pixels);
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
    // The client owns the buffer and may destroy it at any time (it is
    // disconnected: its buffers go before the surface they were committed to).
    // Re-check the handle right before touching it: WaylandBufferRef clears it
    // from a destroy listener, so a null here means the pixels are gone.
    wl_resource* handle = buffer.handle();
    wl_shm_buffer* shm = handle ? wl_shm_buffer_get(handle) : nullptr;
    if (!shm)
        return Frame{};
    // One pass: copy, make an XRGB frame opaque (the client does not have to
    // fill the alpha channel, and the painter blends with it) and swap the
    // channels. A fullscreen frame is 2.4M pixels, so a second pass over it is
    // worth avoiding.
    const std::uint32_t opacity =
        wl_shm_buffer_get_format(shm) == WL_SHM_FORMAT_XRGB8888 ? 0xff000000u : 0u;
    wl_shm_buffer_begin_access(shm);
    for (int y = 0; y < size.height; ++y) {
        const auto* source = reinterpret_cast<const std::uint32_t*>(
            data + static_cast<std::size_t>(y) * stride);
        std::uint32_t* target = frame.pixels.data() + static_cast<std::size_t>(y) * size.width;
        for (int x = 0; x < size.width; ++x) {
            const std::uint32_t pixel = source[x] | opacity;
            target[x] = (pixel & 0xff00ff00u) | ((pixel & 0x00ff0000u) >> 16) |
                        ((pixel & 0x000000ffu) << 16);
        }
    }
    wl_shm_buffer_end_access(shm);
    return frame;
}

std::unique_ptr<Render::RenderTexture> WaylandSurfaceGrabber::importTexture(
    WaylandSurface* surface, Render::RenderDevice* device) const {
    // FIRMIR_WAYLAND_ZEROCOPY=0 forces the copied frame. It is the escape
    // hatch for a driver that reads a client's dma-buf with the wrong channel
    // order: the copy goes through the pixels the compositor can check.
    static const bool zeroCopy = [] {
        const char* value = std::getenv("FIRMIR_WAYLAND_ZEROCOPY");
        return !value || std::strcmp(value, "0") != 0;
    }();
    if (!zeroCopy)
        return nullptr;
    // Every silent return below is a trace, not a report: the caller copies the
    // frame instead, so a window stays visible either way. With
    // FIRMIR_WAYLAND_DEBUG=1 the line names the step that could not deliver a
    // texture, which is what tells a format/modifier problem (the render device
    // refuses the descriptor) from a descriptor one (the backend has none).
    if (!surface || !device || !device->supportsDmaBufTextures()) {
        if (debugEnabled())
            std::fprintf(stderr,
                         "firmir-wayland-surface: importTexture: the render device cannot "
                         "import dma-buf textures; the frame is copied instead\n");
        return nullptr;
    }
    BufferBackend* backend = BufferBackends::selected();
    if (!backend) {
        if (debugEnabled())
            std::fprintf(stderr,
                         "firmir-wayland-surface: importTexture: no buffer backend reads a "
                         "client frame as a dma-buf; the frame is copied instead\n");
        return nullptr;
    }
    // Same lock as grab(): the client cannot replace the buffer under the
    // import, and commit() releases the old one only afterwards.
    std::lock_guard<std::mutex> bufferLock(surface->d->bufferMutex);
    const WaylandBufferRef buffer = surface->d->buffer;
    if (!buffer.valid() || buffer.isShm())
        return nullptr;
    // The client can destroy the buffer while its planes are being imported;
    // the guard makes that wait until the import is done.
    const std::shared_lock<std::shared_mutex> readable = buffer.readLock();
    std::unique_ptr<ImportedBuffer> imported = backend->import(buffer);
    if (!imported || !imported->valid()) {
        if (debugEnabled())
            std::fprintf(stderr,
                         "firmir-wayland-surface: importTexture: backend '%s' could not read "
                         "the client buffer; the frame is copied instead\n",
                         backend->name());
        return nullptr;
    }
    Render::DmaBufTextureDesc desc;
    if (!imported->dmaBuf(desc)) {
        if (debugEnabled())
            std::fprintf(stderr,
                         "firmir-wayland-surface: importTexture: backend '%s' has no dma-buf "
                         "descriptor for the %dx%d client buffer; the frame is copied "
                         "instead\n",
                         backend->name(), imported->width(), imported->height());
        return nullptr;
    }
    // Still inside the import's lifetime: importDmaBufTexture() duplicates the
    // fds, so the texture outlives it.
    std::unique_ptr<Render::RenderTexture> texture(device->importDmaBufTexture(desc));
    if (texture && debugEnabled())
        std::fprintf(stderr,
                     "firmir-wayland-surface: zero-copy import %dx%d fourcc=%08x "
                     "modifier=%016llx\n",
                     desc.width, desc.height, desc.fourcc,
                     static_cast<unsigned long long>(desc.modifier));
    if (!texture && debugEnabled())
        std::fprintf(stderr,
                     "firmir-wayland-surface: importTexture: the render device could not "
                     "import the %dx%d client buffer; the frame is copied instead\n",
                     desc.width, desc.height);
    return texture;
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
float WaylandGuiItem::cornerRadius() const { return d->cornerRadius; }
void WaylandGuiItem::setCornerRadius(float radius) {
    const float clamped = radius > 0.0f ? radius : 0.0f;
    if (d->cornerRadius == clamped)
        return;
    {
        // The render thread reads the radius while painting the picture.
        auto lock = lockScene();
        d->cornerRadius = clamped;
    }
    update();
}
WaylandSeat* WaylandGuiItem::seat() const { return d->seat; }
void WaylandGuiItem::setSeat(WaylandSeat* seat) { d->seat = seat; }
bool WaylandGuiItem::cpuFallbackEnabled() const {
    return d->cpuFallbackSet ? d->cpuFallback : cpuFallbackPolicy();
}
void WaylandGuiItem::setCpuFallbackEnabled(bool enabled) {
    if (d->cpuFallbackSet && d->cpuFallback == enabled)
        return;
    d->cpuFallbackSet = true;
    d->cpuFallback = enabled;
    // The picture changes with the next paint: either the copy starts or it
    // stops being made.
    update();
}
bool WaylandGuiItem::cpuFallbackEnabledByDefault() { return cpuFallbackPolicy(); }
void WaylandGuiItem::setCpuFallbackEnabledByDefault(bool enabled) {
    cpuFallbackDefault() = enabled;
}
void WaylandGuiItem::detachSurface() {
    // Hold the scene lock across the swap: the render thread dereferences
    // the surface while painting, and the caller may be destroying it.
    auto lock = lockScene();
    if (d->view) d->view->setSurface(nullptr);
    // The cached frame belongs to the surface that is going away.
    d->frame = WaylandSurfaceGrabber::Frame();
    d->frameSerial = 0;
    d->frameSurface = nullptr;
    // So do the imported textures: their dma-bufs are the client's.
    d->imports.clear();
    d->importDevice = nullptr;
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
std::unique_ptr<Render::RenderTexture> WaylandGuiItem::takeTexture() {
    // The render thread may be painting with the texture right now.
    auto lock = lockScene();
    // The frame this item copied out of the client buffer lives in its own
    // texture (see paint()): that is the picture the window shows, exactly like
    // the newest import, and it is the one to keep.
    std::unique_ptr<Render::RenderTexture> texture = std::move(d->copied);
    d->copyDevice = nullptr;
    d->copyWidth = 0;
    d->copyHeight = 0;
    // The buffer the window shows now is the one used last.
    Impl::ImportedBuffer* newest = nullptr;
    for (auto& entry : d->imports)
        if (entry.texture && (!newest || entry.lastUsed > newest->lastUsed))
            newest = &entry;
    if (!texture && newest)
        texture = std::move(newest->texture);
    // The item paints nothing until the client commits again.
    d->imports.clear();
    d->importDevice = nullptr;
    d->frame = WaylandSurfaceGrabber::Frame();
    d->frameSerial = 0;
    d->frameSurface = nullptr;
    return texture;
}

WaylandSurfaceGrabber::Frame WaylandGuiItem::takeFrame() {
    auto lock = lockScene();
    WaylandSurfaceGrabber::Frame frame = std::move(d->frame);
    d->frame = WaylandSurfaceGrabber::Frame();
    d->frameSerial = 0;
    d->frameSurface = nullptr;
    return frame;
}

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
    // The frame (and the texture the painter caches for this generation) is
    // reused until the client commits something new.
    const std::uint64_t serial = surface->commitSerial();
    // Nothing was ever committed: the window is being created, not blank.
    if (serial == 0) return;

    // FIRMIR_WAYLAND_DUMP: keep the first frame this surface produced, as the
    // compositor received it, for a visual comparison with the screen.
    if (!d->dumped && dumpEnabled()) {
        d->dumped = true;
        dumpFrame(surface->buffer(), WaylandSurfaceGrabber().grab(surface));
    }

    // Zero copy first: a client that draws with the GPU hands its frame over as
    // a dma-buf, and the render device samples it as a texture. Nothing is
    // copied through the CPU — a fullscreen frame of such a window is ~10 MB.
    if (Render::RenderDevice* device = painter.device()) {
        if (d->importDevice != device) {
            // Another render device: its textures cannot be sampled here.
            d->imports.clear();
            d->importDevice = device;
        }
        const WaylandBufferRef buffer = surface->buffer();
        Impl::ImportedBuffer* entry = nullptr;
        // Only dma-buf buffers: a wl_shm client usually allocates a fresh
        // buffer per commit, so caching those would pin client memory for
        // nothing — the copy below does not need it.
        if (buffer.valid() && !buffer.isShm()) {
            for (auto& candidate : d->imports)
                if (candidate.buffer.handle() == buffer.handle())
                    entry = &candidate;
            if (!entry) {
                // A buffer the window has not shown before.
                if (d->imports.size() >= kImportedBuffers)
                    d->imports.erase(std::min_element(
                        d->imports.begin(), d->imports.end(),
                        [](const auto& a, const auto& b) { return a.lastUsed < b.lastUsed; }));
                d->imports.push_back(Impl::ImportedBuffer());
                entry = &d->imports.back();
                entry->buffer = buffer;
            }
            // A failed import is not final: the device may not have been ready
            // for this buffer when it first appeared, and a client may hand the
            // same wl_buffer over again after drawing into it. One attempt per
            // commit — not per painted frame, which would cost an import for
            // every frame the window is on screen.
            if (!entry->texture && entry->triedSerial != serial) {
                entry->triedSerial = serial;
                entry->texture = WaylandSurfaceGrabber().importTexture(surface, device);
            }
            entry->lastUsed = ++d->importClock;
        }
        Render::RenderTexture* texture = entry ? entry->texture.get() : nullptr;
        if (texture) {
            const Render::TextureDesc& desc = texture->desc();
            if (desc.width > 0 && implicitWidth() != desc.width)
                setImplicitWidth(desc.width);
            if (desc.height > 0 && implicitHeight() != desc.height)
                setImplicitHeight(desc.height);
            painter.drawRoundedTexture(
                Gui::RectF{0, 0,
                           width() > 0 ? width() : static_cast<float>(desc.width),
                           height() > 0 ? height() : static_cast<float>(desc.height)},
                d->cornerRadius, texture);
            // The surface has a texture now: everything this item copied for it
            // before (the pixels, or the texture it uploaded them into) is a
            // second full copy of the same picture — ~10 MB for a fullscreen
            // window — and is dropped. A surface whose import starts failing
            // again is copied again, the next paint re-reads it.
            d->copied.reset();
            d->copyDevice = nullptr;
            d->copyWidth = 0;
            d->copyHeight = 0;
            d->frame = WaylandSurfaceGrabber::Frame{};
            d->frameSerial = 0;
            d->frameSurface = nullptr;
            return;
        }
    }

    // wl_shm, or a device that cannot import client buffers: copy the frame out
    // of the buffer and upload it. That is a full-frame read + upload per commit
    // (~10 MB each way at panel size), so the window cannot hold the display
    // rate; the pixels are released the moment they are a texture (below).
    const WaylandBufferRef current = surface->buffer();
    // attach(null) or nothing attached yet: the client asked for an empty
    // window. Nothing to draw, and the frame counts as handled.
    if (!current.valid()) {
        d->frameSerial = serial;
        d->frameSurface = surface;
        return;
    }
    // A buffer the zero-copy path could not deliver is not copied when the
    // compositor says so: the copy is a full frame per commit, and a client the
    // compositor cannot draw as a texture would otherwise cost more than the
    // rest of the scene together. wl_shm never had a texture to import, so the
    // copy is its normal path, not a fallback. This is not logged as a fault: a
    // window that stays empty is the intended outcome, and only the debug trace
    // below names the decision.
    //
    // The switch is about frames the GPU path *could* have delivered. A device
    // that cannot import client buffers at all — no dma-buf textures, or no
    // buffer backend to read the descriptor — can never draw one of them, and
    // copying is then the only way the client is seen on screen at all; that
    // case keeps the copy so a client is never invisible for no reason.
    Render::RenderDevice* device = painter.device();
    const bool couldImport = device && device->supportsDmaBufTextures() &&
                             BufferBackends::selected();
    if (!current.isShm() && couldImport && !cpuFallbackEnabled()) {
        // One line per commit, not per painted frame: the window asks for this
        // on every frame it is on screen.
        if (debugEnabled() && (d->frameSurface != surface || serial != d->frameSerial))
            std::fprintf(stderr,
                         "firmir-wayland-surface: the client frame is not a texture and the "
                         "CPU copy is off (FIRMIR_WAYLAND_CPU_FALLBACK=1 copies it); the "
                         "window stays empty\n");
        d->frameSerial = serial;
        d->frameSurface = surface;
        return;
    }
    // A texture of a device the painter no longer uses cannot be sampled here:
    // the frame is read again on the current one.
    if (d->copied && d->copyDevice != device) {
        d->copied.reset();
        d->copyDevice = nullptr;
        d->frameSerial = 0;
        d->frameSurface = nullptr;
    }
    if (d->frameSurface != surface || serial != d->frameSerial) {
        WaylandSurfaceGrabber::Frame frame =
            WaylandSurfaceGrabber().grab(surface);
        d->frameSerial = serial;
        d->frameSurface = surface;
        if (frame.pixels.empty())
            return; // the surface will not change until the client commits again
        d->copyWidth = frame.width;
        d->copyHeight = frame.height;
        d->copied.reset();
        d->frame = WaylandSurfaceGrabber::Frame{};
        if (device) {
            // Uploaded here rather than through Painter::drawRgba(): the item
            // then owns the texture and releases the pixels right away, instead
            // of living next to them in the painter's cache — a fullscreen
            // client frame is ~10 MB and every window would hold two copies.
            Render::TextureDesc desc;
            desc.width = frame.width;
            desc.height = frame.height;
            desc.sampler.minFilter = Render::Filter::Linear;
            desc.sampler.magFilter = Render::Filter::Linear;
            std::unique_ptr<Render::RenderTexture> texture(
                device->createTexture(desc));
            if (texture &&
                texture->write(frame.pixels.data(),
                               frame.pixels.size() * sizeof(std::uint32_t))) {
                d->copied = std::move(texture);
                d->copyDevice = device;
            }
        }
        // No render device (or no texture from it): the pixels are the only
        // copy there is, and the painter uploads them itself.
        if (!d->copied)
            d->frame = std::move(frame);
        ++d->generation;
    }
    if (d->copied) {
        if (implicitWidth() != d->copyWidth) setImplicitWidth(d->copyWidth);
        if (implicitHeight() != d->copyHeight) setImplicitHeight(d->copyHeight);
        painter.drawRoundedTexture(
            Gui::RectF{0, 0,
                       width() > 0 ? width() : static_cast<float>(d->copyWidth),
                       height() > 0 ? height() : static_cast<float>(d->copyHeight)},
            d->cornerRadius, d->copied.get());
        return;
    }
    const WaylandSurfaceGrabber::Frame& frame = d->frame;
    if (frame.pixels.empty()) return;
    if (implicitWidth() != frame.width) setImplicitWidth(frame.width);
    if (implicitHeight() != frame.height) setImplicitHeight(frame.height);
    painter.drawRoundedRgba(Gui::RectF{0, 0,
                                       width() > 0 ? width() : static_cast<float>(frame.width),
                                       height() > 0 ? height() : static_cast<float>(frame.height)},
                            d->cornerRadius, frame.pixels.data(), frame.width,
                            frame.height, this, d->generation);
}
WaylandGuiItem* WaylandGuiShellIntegration::createItem(WaylandView* view, Object* parent) {
    auto* item = new WaylandGuiItem(parent);
    item->setView(view);
    return item;
}

} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
