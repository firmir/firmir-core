// VideoSource plugin loading: decode backends live in
// firmir-mm-<name> plugins (.so/.dll; plugins/multimedia/<name>/), found in
// FIRMIR_MULTIMEDIA_PLUGIN_PATH (separator: ':' on Linux, ';' on Windows),
// then `plugins/multimedia` relative to the running executable, then the
// system plugin directory.

#include <firmir/detail/dynload.hpp>
#include <firmir/log.hpp>
#include <firmir/multimedia/mmplugin.hpp>
#include <firmir/multimedia/videosource.hpp>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>

namespace Firmir {
namespace Multimedia {

class VideoSource::Impl {
public:
    std::unique_ptr<VideoSourceBackend> backend;
};


namespace {

const LoggingCategory& lcMm() {
    static const LoggingCategory category("multimedia");
    return category;
}

/// Directories searched for firmir-mm-<name> plugins.
std::vector<std::string> pluginDirs() {
    std::vector<std::string> dirs = detail::pluginSearchDirs(
        "FIRMIR_MULTIMEDIA_PLUGIN_PATH",
        {"../plugins/multimedia", "../../plugins/multimedia",
         "../../../plugins/multimedia", "plugins/multimedia"});
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
    const std::string file =
        "firmir-mm-" + name + detail::sharedLibrarySuffix();
    for (const std::string& dir : pluginDirs()) {
        const std::string path = dir + "/" + file;
        void* handle = detail::libraryOpen(path);
        if (!handle)
            continue;
        auto entry = reinterpret_cast<MmPluginEntry>(
            detail::librarySymbol(handle, FIRMIR_MM_PLUGIN_ENTRY));
        const MmSourcePlugin* plugin = entry ? entry() : nullptr;
        if (!plugin || plugin->abiVersion != FIRMIR_MM_PLUGIN_ABI) {
            cLogWarning(lcMm()) << "bad plugin ABI in" << path;
            detail::libraryClose(handle);
            continue;
        }
        if (plugin->available && !plugin->available()) {
            cLogInfo(lcMm()) << "multimedia backend unavailable:" << name;
            detail::libraryClose(handle);
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
    const std::string suffix = detail::sharedLibrarySuffix();
    std::error_code ec;
    for (const std::string& dir : pluginDirs()) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            const std::string file = entry.path().filename().string();
            if (file.size() > 10 + suffix.size() &&
                file.compare(0, 10, "firmir-mm-") == 0 &&
                file.compare(file.size() - suffix.size(), suffix.size(),
                             suffix) == 0) {
                const std::string name =
                    file.substr(10, file.size() - 10 - suffix.size());
                if (std::find(names.begin(), names.end(), name) ==
                    names.end())
                    names.push_back(name);
            }
        }
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
    : Object(parent), d(std::make_unique<Impl>()) {
    std::string name = backend.toStdString();
    if (name.empty() || name == "auto") {
        const auto backends = availableVideoSourceBackends();
        name = backends.empty() ? std::string() : backends.front();
    }
    if (name.empty())
        return;
    const MmSourcePlugin* plugin = loadPlugin(name);
    if (plugin && plugin->create)
        d->backend.reset(plugin->create(this));
}

VideoSource::VideoSource(std::unique_ptr<VideoSourceBackend> backend,
                         Object* parent)
    : Object(parent), d(std::make_unique<Impl>()) {
    d->backend = std::move(backend);
    if (d->backend)
        d->backend->setHost(this);
}

VideoSource::~VideoSource() = default;

bool VideoSource::isValid() const { return d->backend != nullptr; }

bool VideoSource::open(const String& uri) {
    if (!d->backend) {
        emitErrorMessage(String("no multimedia backend available"));
        return false;
    }
    return d->backend->open(uri);
}
void VideoSource::close() {
    if (d->backend)
        d->backend->close();
}
void VideoSource::play() {
    if (d->backend)
        d->backend->play();
}
void VideoSource::pause() {
    if (d->backend)
        d->backend->pause();
}
void VideoSource::stop() {
    if (d->backend)
        d->backend->stop();
}
VideoSource::State VideoSource::state() const {
    return d->backend ? d->backend->state() : State::Stopped;
}
double VideoSource::duration() const {
    return d->backend ? d->backend->duration() : -1.0;
}
double VideoSource::position() const {
    return d->backend ? d->backend->position() : 0.0;
}
void VideoSource::seek(double seconds) {
    if (d->backend)
        d->backend->seek(seconds);
}
void VideoSource::videoSize(int& width, int& height) const {
    if (d->backend)
        d->backend->videoSize(width, height);
    else {
        width = 0;
        height = 0;
    }
}
bool VideoSource::hasVideo() const { return d->backend && d->backend->hasVideo(); }
bool VideoSource::hasAudio() const { return d->backend && d->backend->hasAudio(); }
const char* VideoSource::backendName() const {
    return d->backend ? d->backend->backendName() : "none";
}
VideoMetrics VideoSource::metrics() const {
    return d->backend ? d->backend->metrics() : VideoMetrics{};
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
