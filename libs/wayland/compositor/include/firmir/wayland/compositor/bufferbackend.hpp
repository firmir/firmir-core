#pragma once

// Plugin ABI and loader for the firmir-wayland-compositor buffer backends.
//
// The compositor has to turn the buffers its clients attach into pixels its
// painter can draw. A buffer backend does exactly that and owns the
// acceleration decision (import an EGLImage, map a wl_shm buffer, ...). The
// library never links one: plugins are discovered as
// `firmir-wayland-compositor-<name>.so` in the directories listed in
// FIRMIR_WAYLAND_COMPOSITOR_PLUGIN_PATH (separator-separated) and next to the
// running executable (build and installed layouts), and the best available one
// is picked when the compositor is created (WaylandCompositor::create()):
//   FIRMIR_WAYLAND_COMPOSITOR_BACKENDS — ordered, comma-separated list to try,
//                                        defaults to "drm_egl,shm";
//   FIRMIR_WAYLAND_COMPOSITOR_BACKEND  — pins one backend (no fallback).
// A plugin returns nullptr from its factory when it cannot serve the display
// (no EGL, no EGL_WL_bind_wayland_display, ...) and the loader tries the next
// name. Without any plugin the library reads wl_shm buffers itself, so a tree
// without the plugins still shows client windows (slower path, logged once).
//
// The interface is header-only: a plugin needs the library only for
// WaylandBufferRef.

#include <firmir/wayland/compositor/core.hpp>

#include <firmir/render/types.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct wl_display;

namespace Firmir {
namespace Wayland {
namespace Compositor {

class BufferBackend;

/// CPU view of one client buffer for the duration of one frame copy. The
/// destructor releases whatever the import pinned (wl_shm access, EGLImage,
/// mapped dma-buf), so it must outlive the pixels read.
class ImportedBuffer {
public:
    virtual ~ImportedBuffer() = default;
    /// False when the buffer could not be read (unsupported format, failed
    /// import): the compositor then leaves the window blank for this frame.
    virtual bool valid() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    /**
     * @brief The buffer as a dma-buf the render device can import.
     *
     * Filling @p desc lets the compositor hand the client's buffer to the GPU
     * as a texture, so the frame never passes through the CPU. The fds stay
     * owned by this import: they are valid until the ImportedBuffer is
     * destroyed, and a successful RenderDevice::importDmaBufTexture() keeps
     * them alive for the texture (it duplicates them).
     *
     * False for a wl_shm buffer and for a backend without a GPU view; the
     * compositor then paints pixels() instead.
     */
    virtual bool dmaBuf(Firmir::Render::DmaBufTextureDesc& /*desc*/) const { return false; }
    /// Tightly packed ARGB8888 rows, width() pixels per row — the fallback for
    /// a buffer without a dma-buf view. The compositor moves the vector out (no
    /// second copy of the frame).
    virtual std::vector<std::uint32_t>& pixels() = 0;
    /**
     * @brief True when pixels() already holds the bytes the painter uploads.
     *
     * pixels() is documented as 0xAARRGGBB words, which the grabber swaps into
     * the painter's R,G,B,A order — a pass over every pixel of the frame (2.4M
     * for a fullscreen tablet window). A backend that reads its pixels straight
     * out of GL with glReadPixels(GL_RGBA) has them in that order already and
     * says so here, so the swap is skipped instead of performed twice.
     */
    virtual bool pixelsAreRgba() const { return false; }
};

/// Buffer reader of one backend.
class BufferBackend {
public:
    virtual ~BufferBackend() = default;
    /// Backend id; it names the plugin file
    /// (firmir-wayland-compositor-<name>).
    virtual const char* name() const = 0;
    /// Prepares @p buffer for reading; nullptr when this backend cannot read
    /// it at all (as opposed to an unreadable frame, which is a valid() false
    /// ImportedBuffer).
    virtual std::unique_ptr<ImportedBuffer> import(const WaylandBufferRef& buffer) = 0;
};

/// Factory exported by a compositor buffer backend plugin.
using CreateBufferBackendFunc = BufferBackend* (*)(wl_display* display);

/// Name of the factory symbol (see CreateBufferBackendFunc).
#define FIRMIR_WAYLAND_COMPOSITOR_CREATE_BACKEND \
    "firmir_wayland_compositor_create_backend"

namespace BufferBackends {

/// Backends to try, best first: FIRMIR_WAYLAND_COMPOSITOR_BACKENDS, else
/// {"drm_egl", "shm"}.
std::vector<std::string> candidates();
/// Directories searched for plugins.
std::vector<std::string> searchDirs();
/// Plugin file name of backend @p name, e.g.
/// "firmir-wayland-compositor-drm-egl.so" (a '_' becomes '-').
std::string pluginFileName(const std::string& name);
/// Installs the first backend that accepts @p display and returns it; nullptr
/// when no plugin could serve it (see the note about the built-in reader).
BufferBackend* create(wl_display* display);
/// The backend installed by create(), or nullptr.
BufferBackend* selected();
/// Name of the installed backend ("" when none).
std::string selectedName();

} // namespace BufferBackends
} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
