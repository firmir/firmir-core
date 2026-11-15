#include <firmir/wayland/compositor/bufferbackend.hpp>

#include <firmir/detail/dynload.hpp>

#include <cstdio>
#include <cstdlib>

namespace Firmir {
namespace Wayland {
namespace Compositor {
namespace BufferBackends {

namespace {

std::string fileNameOf(const std::string& name) {
    return Firmir::detail::pluginFileName("firmir-wayland-compositor-", name);
}

std::unique_ptr<BufferBackend> gSelected;

/// A backend is bound to the wl_display it was created for: the accelerated one
/// binds EGL to it, and that creates a wl_drm global there. The backend has to
/// go away while the display is still valid — a process-exit destructor runs
/// after wl_display_destroy(), and the driver's eglUnbindWaylandDisplayWL()
/// then destroys a global through a display that is already freed.
wl_listener gDisplayDestroyed = {};
bool gWatchingDisplay = false;

void displayDestroyed(wl_listener*, void*) {
    gWatchingDisplay = false;
    gSelected.reset();
}

/// Destroys @p backend together with @p display, before the display's own
/// teardown reaches the globals (wl_display_destroy() emits the destroy signal
/// first).
void watchDisplay(wl_display* display) {
    if (gWatchingDisplay) {
        // Older libwayland has no wl_listener_remove().
        wl_list_remove(&gDisplayDestroyed.link);
        gWatchingDisplay = false;
    }
    gDisplayDestroyed.notify = displayDestroyed;
    wl_display_add_destroy_listener(display, &gDisplayDestroyed);
    gWatchingDisplay = true;
}

} // namespace

std::vector<std::string> candidates() {
    if (const char* pinned = std::getenv("FIRMIR_WAYLAND_COMPOSITOR_BACKEND")) {
        std::vector<std::string> one = Firmir::detail::splitPluginList(pinned);
        if (!one.empty())
            return one;
    }
    if (const char* list = std::getenv("FIRMIR_WAYLAND_COMPOSITOR_BACKENDS")) {
        std::vector<std::string> names = Firmir::detail::splitPluginList(list);
        if (!names.empty())
            return names;
    }
    // Accelerated first: drm_egl imports client buffers through EGL and reads
    // wl_shm buffers itself, so it is a superset that cannot be worse than the
    // plain shm reader.
    return {"drm_egl", "shm"};
}

std::vector<std::string> searchDirs() {
    std::vector<std::string> dirs = Firmir::detail::pluginSearchDirs(
        "FIRMIR_WAYLAND_COMPOSITOR_PLUGIN_PATH",
        {"../plugins/wayland/compositor", "../../plugins/wayland/compositor",
         "../../../plugins/wayland/compositor", "plugins/wayland/compositor"});
    // Where this library itself was loaded from:
    // <libdir>/firmir/plugins/wayland/compositor. An installed tree keeps the
    // plugins next to its libraries, so this works even when the configure-time
    // prefix differs from the install prefix (or when the tree was relocated).
    const std::string libDir = Firmir::detail::libraryDir(
        reinterpret_cast<const void*>(&BufferBackends::create));
    if (!libDir.empty())
        dirs.push_back(libDir + "/firmir/plugins/wayland/compositor");
#ifdef FIRMIR_SYSTEM_PLUGIN_DIR
    dirs.push_back(std::string(FIRMIR_SYSTEM_PLUGIN_DIR) +
                   "/wayland/compositor");
#endif
    return dirs;
}

std::string pluginFileName(const std::string& name) { return fileNameOf(name); }

BufferBackend* create(wl_display* display) {
    gSelected.reset();
    if (!display)
        return nullptr;
    const std::vector<std::string> dirs = searchDirs();
    for (const std::string& name : candidates()) {
        const std::string file = fileNameOf(name);
        void* handle = nullptr;
        std::string path;
        for (const std::string& dir : dirs) {
            path = dir + "/" + file;
            if (!Firmir::detail::fileExists(path))
                continue; // not in this directory
            handle = Firmir::detail::libraryOpen(path);
            if (handle)
                break;
            // The file is there but the loader refused it (missing
            // dependency, wrong architecture, ...): say why.
            std::fprintf(stderr,
                         "firmir-wayland-compositor: cannot load %s: %s\n",
                         path.c_str(), Firmir::detail::libraryError().c_str());
        }
        if (!handle)
            continue; // missing or unloadable; reported above / below
        auto createBackend = reinterpret_cast<CreateBufferBackendFunc>(
            Firmir::detail::librarySymbol(
                handle, FIRMIR_WAYLAND_COMPOSITOR_CREATE_BACKEND));
        if (!createBackend) {
            std::fprintf(stderr,
                         "firmir-wayland-compositor: %s exports no %s\n",
                         path.c_str(), FIRMIR_WAYLAND_COMPOSITOR_CREATE_BACKEND);
            Firmir::detail::libraryClose(handle);
            continue;
        }
        std::unique_ptr<BufferBackend> backend(createBackend(display));
        if (!backend) {
            // The plugin declined this display (no EGL, no
            // EGL_WL_bind_wayland_display, ...): try the next name.
            Firmir::detail::libraryClose(handle);
            continue;
        }
        // The plugin stays loaded for the lifetime of the process: the
        // backend's code lives in it, so closing it would unmap the vtable.
        std::fprintf(stderr, "firmir-wayland-compositor: buffer backend '%s'\n",
                     name.c_str());
        gSelected = std::move(backend);
        // ... and it dies with the display it was created for (see
        // watchDisplay()).
        watchDisplay(display);
        break;
    }
    if (!gSelected) {
        // Not fatal: WaylandSurfaceGrabber::grab() reads wl_shm buffers
        // itself. Say where the plugins were looked for.
        std::fprintf(stderr,
                     "firmir-wayland-compositor: no buffer backend plugin "
                     "loaded (searched, and "
                     "FIRMIR_WAYLAND_COMPOSITOR_PLUGIN_PATH), reading "
                     "wl_shm buffers directly:\n");
        for (const std::string& dir : dirs)
            std::fprintf(stderr, "firmir-wayland-compositor:   %s\n",
                         dir.c_str());
    }
    return gSelected.get();
}

BufferBackend* selected() { return gSelected.get(); }

std::string selectedName() {
    return gSelected ? gSelected->name() : std::string();
}

} // namespace BufferBackends
} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
