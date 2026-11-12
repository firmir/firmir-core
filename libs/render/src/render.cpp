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

} // namespace

RenderBackend* Render::backend(const std::string& name) {
    const std::vector<std::string> candidates = backendCandidates(name);
    std::lock_guard<std::mutex> lock(g_backendsMutex);
    for (const std::string& backendName : candidates) {
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
        if (candidates.size() == 1)
            cLogWarning(lcRender()) << "render backend not found:" << backendName;
        else
            cLogInfo(lcRender())
                << "render backend unavailable:" << backendName;
    }
    return nullptr;
}

std::unique_ptr<RenderDevice> Render::createDevice(const std::string& name,
                                                   const DeviceDesc& desc) {
    RenderBackend* selected = backend(name);
    if (!selected)
        return nullptr;
    return std::unique_ptr<RenderDevice>(selected->createDevice(desc));
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
