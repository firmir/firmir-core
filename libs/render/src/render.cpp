#include <firmir/render/render.hpp>

#include <firmir/log.hpp>

#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#include <utility>
#include <vector>
#include <unistd.h>

namespace Firmir {
namespace Render {

namespace {

const LoggingCategory& lcRender() {
    static const LoggingCategory category("render");
    return category;
}

/// Directories searched for firmir-render-<name>.so plugins:
/// FIRMIR_RENDER_PLUGIN_PATH (colon-separated), then `plugins/render`
/// relative to the running executable (build tree and installed layouts).
std::vector<std::string> pluginDirs() {
    std::vector<std::string> dirs;
    const char* path = std::getenv("FIRMIR_RENDER_PLUGIN_PATH");
    if (path && *path) {
        std::string rest = path;
        for (;;) {
            const std::size_t colon = rest.find(':');
            dirs.push_back(rest.substr(0, colon));
            if (colon == std::string::npos)
                break;
            rest.erase(0, colon + 1);
        }
    }
    char exePath[4096];
    const ssize_t len =
        ::readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        std::string dir = exePath;
        const std::size_t slash = dir.rfind('/');
        if (slash != std::string::npos) {
            dir.erase(slash);
            dirs.push_back(dir + "/../plugins/render");
            dirs.push_back(dir + "/../../plugins/render");
            dirs.push_back(dir + "/plugins/render");
        }
    }
    return dirs;
}

std::string resolveName(const std::string& name) {
    if (!name.empty())
        return name;
    const char* env = std::getenv("FIRMIR_RENDER_BACKEND");
    if (env && *env)
        return env;
    return "null";
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
    const std::string backendName = resolveName(name);
    std::lock_guard<std::mutex> lock(g_backendsMutex);
    for (const auto& loaded : g_backends) {
        if (loaded.name == backendName)
            return loaded.backend;
    }

    const std::string file = "firmir-render-" + backendName + ".so";
    for (const std::string& dir : pluginDirs()) {
        const std::string path = dir + "/" + file;
        void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle)
            continue;
        auto create = reinterpret_cast<CreateRenderBackendFunc>(
            dlsym(handle, FIRMIR_RENDER_CREATE_BACKEND));
        if (!create) {
            cLogWarning(lcRender())
                << "no" << FIRMIR_RENDER_CREATE_BACKEND << "in" << path;
            dlclose(handle);
            continue;
        }
        RenderBackend* backend = create();
        if (!backend) {
            dlclose(handle);
            continue;
        }
        g_backends.push_back({backendName, handle, backend});
        cLogInfo(lcRender()) << "loaded render backend" << path;
        return backend;
    }

    cLogWarning(lcRender()) << "render backend not found:" << backendName;
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
        dlclose(it->handle);
}

} // namespace Render
} // namespace Firmir
