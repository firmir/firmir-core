#include <firmir/render/render.hpp>

#include <firmir/detail/dynload.hpp>
#include <firmir/log.hpp>

#include <cstdlib>
#include <mutex>
#include <utility>
#include <vector>

namespace Firmir {
namespace Render {

namespace {

const LoggingCategory& lcRender() {
    static const LoggingCategory category("render");
    return category;
}

/// Directories searched for firmir-render-<name> plugins (.so/.dll):
/// FIRMIR_RENDER_PLUGIN_PATH (separator: ':' on Linux, ';' on Windows), then
/// `plugins/render` relative to the running executable (build tree and
/// installed layouts).
std::vector<std::string> pluginDirs() {
    std::vector<std::string> dirs = detail::pluginSearchDirs(
        "FIRMIR_RENDER_PLUGIN_PATH",
        {"../plugins/render", "../../plugins/render", "../../../plugins/render",
         "plugins/render"});
#ifdef FIRMIR_SYSTEM_PLUGIN_DIR
    dirs.push_back(FIRMIR_SYSTEM_PLUGIN_DIR);
#endif
    return dirs;
}

/// Backend selection: the explicit name argument, then
/// FIRMIR_RENDER_BACKEND, otherwise the first loadable plugin of
/// vulkan -> opengl -> soft.
std::vector<std::string> backendCandidates(const std::string& name) {
    if (!name.empty())
        return {name};
    const char* env = std::getenv("FIRMIR_RENDER_BACKEND");
    if (env && *env)
        return {env};
    return {"vulkan", "opengl", "soft"};
}

struct LoadedBackend {
    std::string name;
    void* handle;
    RenderBackend* backend;
};

std::mutex g_backendsMutex;
std::vector<LoadedBackend> g_backends;

/// Returns the already-loaded backend or dlopen()s it; nullptr when the plugin
/// is missing, exports no factory or the factory fails. Caller holds
/// g_backendsMutex.
RenderBackend* loadBackend(const std::string& backendName) {
    for (const auto& loaded : g_backends) {
        if (loaded.name == backendName)
            return loaded.backend;
    }

    const std::string file =
        "firmir-render-" + backendName + detail::sharedLibrarySuffix();
    for (const std::string& dir : pluginDirs()) {
        const std::string path = dir + "/" + file;
        void* handle = detail::libraryOpen(path);
        if (!handle)
            continue;
        auto create = reinterpret_cast<CreateRenderBackendFunc>(
            detail::librarySymbol(handle, FIRMIR_RENDER_CREATE_BACKEND));
        if (!create) {
            cLogWarning(lcRender())
                << "no" << FIRMIR_RENDER_CREATE_BACKEND << "in" << path;
            detail::libraryClose(handle);
            continue;
        }
        RenderBackend* backend = create();
        if (!backend) {
            detail::libraryClose(handle);
            continue;
        }
        g_backends.push_back({backendName, handle, backend});
        cLogInfo(lcRender()) << "loaded render backend" << path;
        return backend;
    }
    return nullptr;
}

/// Logs a backend that could not be loaded at all. An explicitly requested
/// backend is a warning; a missing default candidate is informational, because
/// the selection order continues with the next one.
void logBackendUnavailable(const std::string& backendName,
                           bool explicitBackend) {
    if (explicitBackend)
        cLogWarning(lcRender())
            << "render backend not found:" << backendName;
    else
        cLogInfo(lcRender())
            << "render backend unavailable:" << backendName;
}

} // namespace

RenderBackend* Render::backend(const std::string& name) {
    const std::vector<std::string> candidates = backendCandidates(name);
    std::lock_guard<std::mutex> lock(g_backendsMutex);
    for (const std::string& backendName : candidates) {
        if (RenderBackend* backend = loadBackend(backendName))
            return backend;
        logBackendUnavailable(backendName, candidates.size() == 1);
    }
    return nullptr;
}

std::unique_ptr<RenderDevice> Render::createDevice(const std::string& name,
                                                   const DeviceDesc& desc) {
    // An explicit backend (the name argument or FIRMIR_RENDER_BACKEND) is used
    // as-is. The default order, however, falls through to the next candidate
    // when a plugin loads but cannot create a device - for example OpenGL
    // without a usable EGL display in a sandboxed build still gets a soft
    // device instead of no device at all.
    const std::vector<std::string> candidates = backendCandidates(name);
    const bool explicitBackend = candidates.size() == 1;
    for (const std::string& backendName : candidates) {
        RenderBackend* selected = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_backendsMutex);
            selected = loadBackend(backendName);
        }
        if (!selected) {
            logBackendUnavailable(backendName, explicitBackend);
            continue;
        }
        if (std::unique_ptr<RenderDevice> device{selected->createDevice(desc)})
            return device;
        if (explicitBackend)
            break;
        cLogWarning(lcRender())
            << "render backend" << backendName << "cannot create a device";
    }
    return nullptr;
}

void Render::shutdown() {
    std::vector<LoadedBackend> loaded;
    {
        std::lock_guard<std::mutex> lock(g_backendsMutex);
        loaded.swap(g_backends);
    }
    for (auto it = loaded.rbegin(); it != loaded.rend(); ++it)
        detail::libraryClose(it->handle);
}

} // namespace Render
} // namespace Firmir
