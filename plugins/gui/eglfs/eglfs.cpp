// eglfs display backend for firmir-gui: fullscreen, compositor-less
// presentation through a device integration (see
// <firmir/gui/eglfsintegration.hpp>). This plugin is the eglfs platform layer
// only — it resolves FIRMIR_EGLFS_INTEGRATION (default "kms"), dlopen()s
// firmir-gui-eglfs-<name>.so and routes screens/windows to it. Input comes
// from the evdev input plugin, which Platform loads as the default fallback
// for display backends without their own input factory.
//
// Search path for integrations: FIRMIR_EGLFS_PLUGIN_PATH, then the eglfs/
// subdirectory next to the GUI plugins (build tree and installed layouts).

#include <firmir/detail/dynload.hpp>
#include <firmir/gui/eglfsintegration.hpp>
#include <firmir/log.hpp>

#include <cstdlib>
#include <string>
#include <vector>

using namespace Firmir::Gui;

namespace {

const Firmir::LoggingCategory& lcEglFs() {
    static const Firmir::LoggingCategory category("gui.eglfs");
    return category;
}

/// Directories searched for firmir-gui-eglfs-<name> plugins: an explicit
/// FIRMIR_EGLFS_PLUGIN_PATH, then the eglfs/ subdirectory of the GUI plugin
/// directories (FIRMIR_GUI_PLUGIN_PATH and the executable-relative build /
/// installed layouts).
std::vector<std::string> integrationDirs() {
    std::vector<std::string> dirs =
        Firmir::detail::pluginSearchDirs("FIRMIR_EGLFS_PLUGIN_PATH", {});
    for (const std::string& dir : Firmir::detail::pluginSearchDirs(
             "FIRMIR_GUI_PLUGIN_PATH",
             {"../plugins", "../../plugins", "../../../plugins", "plugins"}))
        dirs.push_back(dir + "/eglfs");
#ifdef FIRMIR_EGLFS_SYSTEM_PLUGIN_DIR
    dirs.push_back(FIRMIR_EGLFS_SYSTEM_PLUGIN_DIR);
#endif
    return dirs;
}

/// DisplayBackend that forwards everything to the selected integration.
class EglFsDisplay : public DisplayBackend {
public:
    EglFsDisplay() {
        const char* selector = std::getenv("FIRMIR_EGLFS_INTEGRATION");
        m_name = eglFsIntegrationName(selector && *selector ? selector : "kms");
        if (m_name.empty()) {
            cLogWarning(lcEglFs())
                << "empty FIRMIR_EGLFS_INTEGRATION; no device integration";
            return;
        }

        const std::string file =
            "firmir-gui-eglfs-" + m_name +
            Firmir::detail::sharedLibrarySuffix();
        for (const std::string& dir : integrationDirs()) {
            const std::string path = dir + "/" + file;
            if (void* handle = Firmir::detail::libraryOpen(path)) {
                m_handle = handle;
                m_path = path;
                break;
            }
        }
        if (!m_handle) {
            cLogWarning(lcEglFs())
                << "device integration" << m_name << "not found:" << file
                << "- searched FIRMIR_EGLFS_PLUGIN_PATH, the GUI plugin "
                   "search path and the executable-relative eglfs directory";
            return;
        }

        auto abi = reinterpret_cast<unsigned (*)()>(
            Firmir::detail::librarySymbol(m_handle,
                                          FIRMIR_EGLFS_INTEGRATION_ABI_SYMBOL));
        if (!abi) {
            cLogWarning(lcEglFs())
                << "device integration" << m_path << "exports no"
                << FIRMIR_EGLFS_INTEGRATION_ABI_SYMBOL << "(ABI mismatch)";
            return;
        }
        if (abi() != FIRMIR_EGLFS_INTEGRATION_ABI) {
            cLogWarning(lcEglFs())
                << "device integration" << m_path << "uses eglfs ABI" << abi()
                << "expected" << FIRMIR_EGLFS_INTEGRATION_ABI
                << "- not loading it";
            return;
        }

        auto create = reinterpret_cast<CreateEglFsIntegrationFunc>(
            Firmir::detail::librarySymbol(m_handle,
                                          FIRMIR_EGLFS_CREATE_INTEGRATION));
        if (!create) {
            cLogWarning(lcEglFs())
                << "device integration" << m_path << "exports no"
                << FIRMIR_EGLFS_CREATE_INTEGRATION;
            return;
        }
        m_integration = create();
        if (!m_integration) {
            cLogWarning(lcEglFs())
                << "device integration" << m_path << "failed to initialize";
            return;
        }
        cLogInfo(lcEglFs()) << "loaded device integration"
                            << m_integration->name() << "from" << m_path;
        m_integration->screenCallback = [this](PlatformScreen* screen,
                                               bool added) {
            // Platform decides whether this runs inline or is posted, so
            // forwarding from any thread is safe.
            if (screenCallback)
                screenCallback(screen, added);
        };
        if (m_integration->screens().empty())
            cLogWarning(lcEglFs()) << "device integration"
                                   << m_integration->name()
                                   << "has no outputs";
    }

    std::vector<PlatformScreen*> screens() override {
        if (!m_integration)
            return {};
        return m_integration->screens();
    }

    PlatformWindow* createWindow(PlatformScreen* screen) override {
        if (!m_integration)
            return nullptr;
        return m_integration->createWindow(screen);
    }

    void destroyWindow(PlatformWindow* window) override {
        if (m_integration)
            m_integration->destroyWindow(window);
    }

    // No destructor cleanup by design: the integration object lives inside
    // the plugin, and Platform owns this backend for the whole process, so
    // the library stays loaded (dlclose() would free the integration while
    // it is still referenced).

private:
    std::string m_name;
    std::string m_path;
    void* m_handle = nullptr;
    EglFsIntegration* m_integration = nullptr;
};

} // namespace

extern "C" DisplayBackend* firmir_gui_create_display() {
    return new EglFsDisplay;
}
