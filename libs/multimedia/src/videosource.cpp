// VideoSource plugin loading: decode backends live in
// firmir-mm-<name>.so (plugins/multimedia/<name>/), found in
// FIRMIR_MULTIMEDIA_PLUGIN_PATH (colon-separated), then
// `plugins/multimedia` relative to the running executable, then the system
// plugin directory.

#include <firmir/log.hpp>
#include <firmir/multimedia/mmplugin.hpp>
#include <firmir/multimedia/videosource.hpp>

#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>
#include <string>

namespace Firmir {
namespace Multimedia {

namespace {

const LoggingCategory& lcMm() {
    static const LoggingCategory category("multimedia");
    return category;
}

/// Directories searched for firmir-mm-<name>.so plugins.
std::vector<std::string> pluginDirs() {
    std::vector<std::string> dirs;
    const char* path = std::getenv("FIRMIR_MULTIMEDIA_PLUGIN_PATH");
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
            dirs.push_back(dir + "/../plugins/multimedia");
            dirs.push_back(dir + "/../../plugins/multimedia");
            dirs.push_back(dir + "/../../../plugins/multimedia");
            dirs.push_back(dir + "/plugins/multimedia");
        }
    }
#ifdef FIRMIR_SYSTEM_PLUGIN_DIR
    dirs.push_back(std::string(FIRMIR_SYSTEM_PLUGIN_DIR) + "/multimedia");
#endif
    return dirs;
}

struct LoadedPlugin {
    std::string name;
    void* handle;
    const MmSourcePlugin* plugin;
};

std::mutex g_pluginsMutex;
std::vector<LoadedPlugin> g_plugins;

/// Loads (and caches) the plugin for @p name; nullptr when not installed or
/// its availability probe fails.
const MmSourcePlugin* loadPlugin(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_pluginsMutex);
    for (const auto& loaded : g_plugins) {
        if (loaded.name == name)
            return loaded.plugin;
    }
    const std::string file = "firmir-mm-" + name + ".so";
    for (const std::string& dir : pluginDirs()) {
        const std::string path = dir + "/" + file;
        void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle)
            continue;
        auto entry = reinterpret_cast<MmPluginEntry>(
            dlsym(handle, FIRMIR_MM_PLUGIN_ENTRY));
        const MmSourcePlugin* plugin = entry ? entry() : nullptr;
        if (!plugin || plugin->abiVersion != FIRMIR_MM_PLUGIN_ABI) {
            cLogWarning(lcMm()) << "bad plugin ABI in" << path;
            dlclose(handle);
            continue;
        }
        if (plugin->available && !plugin->available()) {
            cLogInfo(lcMm()) << "multimedia backend unavailable:" << name;
            dlclose(handle);
            continue;
        }
        g_plugins.push_back({name, handle, plugin});
        cLogInfo(lcMm()) << "loaded multimedia backend" << path;
        return plugin;
    }
    cLogInfo(lcMm()) << "multimedia backend not found:" << name;
    return nullptr;
}

/// All installed plugin names (scanned from the plugin directories).
std::vector<std::string> installedBackends() {
    std::vector<std::string> names;
    for (const std::string& dir : pluginDirs()) {
        DIR* dp = opendir(dir.c_str());
        if (!dp)
            continue;
        while (dirent* de = readdir(dp)) {
            const std::string file = de->d_name;
            if (file.size() > 12 && file.compare(0, 10, "firmir-mm-") == 0 &&
                file.compare(file.size() - 3, 3, ".so") == 0) {
                const std::string name = file.substr(10, file.size() - 13);
                if (std::find(names.begin(), names.end(), name) ==
                    names.end())
                    names.push_back(name);
            }
        }
        closedir(dp);
    }
    // "auto" preference: gstreamer, then ffmpeg, then anything else.
    const auto rank = [](const std::string& n) {
        if (n == "gstreamer")
            return 0;
        if (n == "ffmpeg")
            return 1;
        return 2;
    };
    std::stable_sort(names.begin(), names.end(),
                     [&](const std::string& a, const std::string& b) {
                         return rank(a) < rank(b);
                     });
    return names;
}

} // namespace

bool VideoFrame::ensureCpuPixels() {
    if (!rgba.empty())
        return true;
    // The readback exists for GPU frames and for lazy CPU frames (the
    // backend holds the native buffer and copies pixels on first use).
    if (readback)
        return readback(*this);
    return false;
}

std::vector<std::string> availableVideoSourceBackends() {
    std::vector<std::string> backends;
    for (const std::string& name : installedBackends()) {
        if (loadPlugin(name))
            backends.push_back(name);
    }
    return backends;
}

// ---------------------------------------------------------------------------
// VideoSource: concrete facade over a VideoSourceBackend.
// ---------------------------------------------------------------------------

VideoSource::VideoSource(Object* parent)
    : VideoSource(String("auto"), parent) {}

VideoSource::VideoSource(const String& backend, Object* parent)
    : Object(parent) {
    std::string name = backend.toStdString();
    if (name.empty() || name == "auto") {
        const auto backends = availableVideoSourceBackends();
        name = backends.empty() ? std::string() : backends.front();
    }
    if (name.empty())
        return;
    const MmSourcePlugin* plugin = loadPlugin(name);
    if (plugin && plugin->create)
        m_backend.reset(plugin->create(this));
}

VideoSource::VideoSource(std::unique_ptr<VideoSourceBackend> backend,
                         Object* parent)
    : Object(parent), m_backend(std::move(backend)) {
    if (m_backend)
        m_backend->setHost(this);
}

VideoSource::~VideoSource() = default;

bool VideoSource::isValid() const { return m_backend != nullptr; }

bool VideoSource::open(const String& uri) {
    if (!m_backend) {
        emitErrorMessage(String("no multimedia backend available"));
        return false;
    }
    return m_backend->open(uri);
}
void VideoSource::close() {
    if (m_backend)
        m_backend->close();
}
void VideoSource::play() {
    if (m_backend)
        m_backend->play();
}
void VideoSource::pause() {
    if (m_backend)
        m_backend->pause();
}
void VideoSource::stop() {
    if (m_backend)
        m_backend->stop();
}
VideoSource::State VideoSource::state() const {
    return m_backend ? m_backend->state() : State::Stopped;
}
double VideoSource::duration() const {
    return m_backend ? m_backend->duration() : -1.0;
}
double VideoSource::position() const {
    return m_backend ? m_backend->position() : 0.0;
}
void VideoSource::seek(double seconds) {
    if (m_backend)
        m_backend->seek(seconds);
}
void VideoSource::videoSize(int& width, int& height) const {
    if (m_backend)
        m_backend->videoSize(width, height);
    else {
        width = 0;
        height = 0;
    }
}
bool VideoSource::hasVideo() const { return m_backend && m_backend->hasVideo(); }
bool VideoSource::hasAudio() const { return m_backend && m_backend->hasAudio(); }
const char* VideoSource::backendName() const {
    return m_backend ? m_backend->backendName() : "none";
}
VideoMetrics VideoSource::metrics() const {
    return m_backend ? m_backend->metrics() : VideoMetrics{};
}

void VideoSource::emitFrameReady(VideoFrame frame) {
    emitSignal<VideoFrame>(signalFrameReady, std::move(frame));
}
void VideoSource::emitStateChanged(State state) {
    emitSignal<int>(signalStateChanged, static_cast<int>(state));
}
void VideoSource::emitErrorMessage(const String& message) {
    emitSignal<String>(signalError, String(message));
}

VideoSource* createVideoSource(const String& backend, Object* parent) {
    auto* source = new VideoSource(backend, parent);
    if (!source->isValid()) {
        delete source;
        return nullptr;
    }
    return source;
}

} // namespace Multimedia
} // namespace Firmir
