#include <firmir/wayland/client/backend.hpp>

#include <firmir/detail/dynload.hpp>

#include <cstdio>
#include <cstdlib>

namespace Firmir {
namespace Wayland {
namespace Client {
namespace Backends {

namespace {

/// Backend id -> plugin file name: "drm_egl" -> "firmir-wayland-client-drm-egl.so".
std::string fileNameOf(const std::string& name) {
    return Firmir::detail::pluginFileName("firmir-wayland-client-", name);
}

std::string gSelected;

} // namespace

std::vector<std::string> candidates() {
    if (const char* pinned = std::getenv("FIRMIR_WAYLAND_CLIENT_BACKEND")) {
        std::vector<std::string> one = Firmir::detail::splitPluginList(pinned);
        if (!one.empty())
            return one;
    }
    if (const char* list = std::getenv("FIRMIR_WAYLAND_CLIENT_BACKENDS")) {
        std::vector<std::string> names = Firmir::detail::splitPluginList(list);
        if (!names.empty())
            return names;
    }
    // Accelerated first: a GPU backend hands its buffers over without a copy;
    // wl_shm is the portable one that always works.
    return {"drm_egl", "shm"};
}

std::vector<std::string> searchDirs() {
    std::vector<std::string> dirs = Firmir::detail::pluginSearchDirs(
        "FIRMIR_WAYLAND_CLIENT_PLUGIN_PATH",
        {"../plugins/wayland/client", "../../plugins/wayland/client",
         "../../../plugins/wayland/client", "plugins/wayland/client"});
    // Where this library itself was loaded from: <libdir>/firmir/plugins/wayland/client.
    // An installed tree keeps the plugins next to its libraries, so this works
    // even when the configure-time prefix differs from the install prefix (or
    // when the tree was relocated).
    const std::string libDir = Firmir::detail::libraryDir(
        reinterpret_cast<const void*>(&Backends::create));
    if (!libDir.empty())
        dirs.push_back(libDir + "/firmir/plugins/wayland/client");
#ifdef FIRMIR_SYSTEM_PLUGIN_DIR
    dirs.push_back(std::string(FIRMIR_SYSTEM_PLUGIN_DIR) + "/wayland/client");
#endif
    return dirs;
}

std::string pluginFileName(const std::string& name) { return fileNameOf(name); }

std::unique_ptr<Backend> create(const BackendParams& params) {
    gSelected.clear();
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
                         "firmir-wayland-client: cannot load %s: %s\n",
                         path.c_str(), Firmir::detail::libraryError().c_str());
        }
        if (!handle)
            continue; // missing or unloadable; reported above / below
        auto createBackend = reinterpret_cast<CreateBackendFunc>(
            Firmir::detail::librarySymbol(handle,
                                          FIRMIR_WAYLAND_CLIENT_CREATE_BACKEND));
        if (!createBackend) {
            std::fprintf(stderr,
                         "firmir-wayland-client: %s exports no %s\n",
                         path.c_str(), FIRMIR_WAYLAND_CLIENT_CREATE_BACKEND);
            Firmir::detail::libraryClose(handle);
            continue;
        }
        std::unique_ptr<Backend> backend(createBackend(params));
        if (!backend) {
            // The plugin declined this connection (missing GPU, no
            // buffer-sharing protocol, ...): try the next name.
            Firmir::detail::libraryClose(handle);
            continue;
        }
        // The plugin stays loaded for the lifetime of the process: the
        // backend's code lives in it, so closing it would unmap the vtable.
        gSelected = name;
        return backend;
    }
    // Nothing loaded: say where the plugins were looked for (the caller then
    // has no way to present frames).
    std::fprintf(stderr,
                 "firmir-wayland-client: no buffer backend plugin loaded "
                 "(searched, and FIRMIR_WAYLAND_CLIENT_PLUGIN_PATH):\n");
    for (const std::string& dir : dirs)
        std::fprintf(stderr, "firmir-wayland-client:   %s\n", dir.c_str());
    return nullptr;
}

std::string selectedName() { return gSelected; }

} // namespace Backends
} // namespace Client
} // namespace Wayland
} // namespace Firmir
