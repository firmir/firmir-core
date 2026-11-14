#pragma once

// Plugin ABI and loader for the firmir-wayland-client buffer backends.
//
// A backend produces the wl_buffers a client attaches to its surfaces. The
// base library never links one: they are discovered as
// `firmir-wayland-client-<name>.so` in the directories listed in
// FIRMIR_WAYLAND_CLIENT_PLUGIN_PATH (separator-separated) and next to the
// running executable (build and installed layouts), and the best available one
// is picked once per connection:
//   FIRMIR_WAYLAND_CLIENT_BACKENDS — ordered, comma-separated list to try,
//                                    defaults to "drm_egl,shm" (accelerated
//                                    first);
//   FIRMIR_WAYLAND_CLIENT_BACKEND  — pins one backend (no fallback).
// A plugin returns nullptr from its factory when it cannot serve the
// connection (missing EGL, no buffer-sharing protocol, ...), and the loader
// tries the next name.
//
// The interface is header-only on purpose: a plugin needs no library symbols,
// only <wayland-client.h>.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_shm;
struct wl_buffer;

namespace Firmir {
namespace Wayland {
namespace Client {

class BackendBuffer;

/// What a backend needs from the connection. Extend this struct when a new
/// backend needs more: every existing backend must keep ignoring the fields it
/// does not use, so adding a member never breaks a plugin.
struct BackendParams {
    wl_display* display = nullptr;
    wl_registry* registry = nullptr;
    /// The wl_shm global, when the compositor advertised one.
    wl_shm* shm = nullptr;
    /// True when the compositor advertised zwp_linux_dmabuf_v1 — the transport
    /// a GPU backend needs to hand its buffers over. Only probing backends
    /// look at it today (the protocol plugin is not implemented yet).
    bool dmabuf = false;
};

/// One attachable frame buffer.
///
/// The window renders into pixels() and calls commitPixels() before attaching
/// handle(). A backend that renders on the GPU leaves pixels() null and keeps
/// its own staging area: the window then hands the frame to commitPixels()
/// only, which uploads it. (The GUI renderer is CPU-based, so every backend
/// implemented today provides pixels().)
class BackendBuffer {
public:
    virtual ~BackendBuffer() = default;
    /// The wl_buffer to attach to a wl_surface.
    virtual wl_buffer* handle() const = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    /// Bytes per row of pixels() (0 when there is no CPU staging area).
    virtual int stride() const = 0;
    /// CPU staging area the renderer writes into before commitPixels().
    virtual std::uint32_t* pixels() const { return nullptr; }
    /// Bytes of pixels().
    virtual std::size_t bytes() const { return 0; }
    /// Makes the written pixels visible to the compositor: a no-op for wl_shm,
    /// a GL upload for a GPU buffer.
    virtual bool commitPixels() { return true; }
};

/// Frame-buffer factory of one backend.
class Backend {
public:
    virtual ~Backend() = default;
    /// Backend id; it names the plugin file (firmir-wayland-client-<name>).
    virtual const char* name() const = 0;
    /// Allocates a frame buffer of @p width x @p height logical pixels.
    virtual std::unique_ptr<BackendBuffer> createBuffer(int width, int height) = 0;
};

/// Factory exported by a client backend plugin.
using CreateBackendFunc = Backend* (*)(const BackendParams& params);

/// Name of the factory symbol (see CreateBackendFunc).
#define FIRMIR_WAYLAND_CLIENT_CREATE_BACKEND "firmir_wayland_client_create_backend"

namespace Backends {

/// Backends to try, best first: FIRMIR_WAYLAND_CLIENT_BACKENDS, else
/// {"drm_egl", "shm"}.
std::vector<std::string> candidates();
/// Directories searched for plugins (FIRMIR_WAYLAND_CLIENT_PLUGIN_PATH, then
/// executable-relative build/install locations).
std::vector<std::string> searchDirs();
/// Plugin file name of backend @p name, e.g. "firmir-wayland-client-drm-egl.so"
/// (a '_' in the id becomes '-' in the file name).
std::string pluginFileName(const std::string& name);
/// Creates the first backend that accepts @p params; nullptr when none does.
std::unique_ptr<Backend> create(const BackendParams& params);
/// Name of the backend the last create() call selected ("" when none).
std::string selectedName();

} // namespace Backends
} // namespace Client
} // namespace Wayland
} // namespace Firmir
