#pragma once

// Device-integration ABI for the eglfs display backend. The backend
// (firmir-gui-eglfs.so) is the eglfs platform layer: it owns the
// DisplayBackend, screen hotplug delivery and window lifecycle. The device
// specifics (output enumeration, native surfaces, frame presentation) live in
// integrations loaded from firmir-gui-eglfs-<name>.so.
//
// Integration search path: FIRMIR_EGLFS_PLUGIN_PATH (colon-separated on
// Linux, ';' on Windows), then the eglfs/ subdirectory of the GUI plugin
// search directories, then the installed lib/firmir/plugins/gui/eglfs/.
//
// Integration selection: FIRMIR_EGLFS_INTEGRATION, normalized with
// eglFsIntegrationName() — "kms", "eglfs_kms" and "EGLFS-KMS" all select
// firmir-gui-eglfs-kms.so (default: "kms"). Planned integrations:
// kms-egldevice, emu.

#include <firmir/gui/backend.hpp>

#include <cctype>
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Firmir {
namespace Gui {

/// A device integration plugged into the eglfs backend: owns the output
/// device, enumerates its screens and creates fullscreen windows on them.
class EglFsIntegration {
public:
    virtual ~EglFsIntegration() = default;
    /// Integration name ("kms", ...); used for logging only.
    virtual std::string name() const = 0;
    /// Currently connected outputs. Owned by the integration and must stay
    /// valid until the matching screenCallback(..., false) was delivered.
    virtual std::vector<PlatformScreen*> screens() = 0;
    /// Creates a native window on @p screen. The integration owns it.
    virtual PlatformWindow* createWindow(PlatformScreen* screen) = 0;
    /// Destroys a window created with createWindow().
    virtual void destroyWindow(PlatformWindow* window) = 0;
    /// Invoked (from any thread) when an output is plugged in or removed.
    std::function<void(PlatformScreen* screen, bool added)> screenCallback;
};

using CreateEglFsIntegrationFunc = EglFsIntegration* (*)();

/// Normalizes an integration selector: lower case, '_' replaced by '-',
/// surrounding whitespace dropped and a leading "eglfs" prefix removed.
/// "eglfs_kms_egldevice" -> "kms-egldevice", "eglfs_emu" -> "emu".
inline std::string eglFsIntegrationName(const std::string& selector) {
    std::string name;
    name.reserve(selector.size());
    for (const char c : selector) {
        const auto uc = static_cast<unsigned char>(c);
        if (c == '_')
            name.push_back('-');
        else if (c != ' ' && c != '\t')
            name.push_back(static_cast<char>(std::tolower(uc)));
    }
    const std::string prefix = "eglfs";
    if (name.size() > prefix.size() &&
        name.compare(0, prefix.size(), prefix) == 0 &&
        name[prefix.size()] == '-')
        name.erase(0, prefix.size() + 1);
    return name;
}

} // namespace Gui
} // namespace Firmir

/// ABI version of Firmir::Gui::EglFsIntegration; bumped on breaking changes.
#define FIRMIR_EGLFS_INTEGRATION_ABI 1

/// Symbol every integration exports: `extern "C" unsigned
/// firmir_eglfs_integration_abi()` returning FIRMIR_EGLFS_INTEGRATION_ABI.
/// The backend refuses integrations built against another ABI version.
#define FIRMIR_EGLFS_INTEGRATION_ABI_SYMBOL "firmir_eglfs_integration_abi"

/// Symbol every integration exports: `extern "C"
/// Firmir::Gui::EglFsIntegration* firmir_eglfs_create_integration()`. The
/// returned object is owned by the plugin and must outlive the eglfs display
/// backend.
#define FIRMIR_EGLFS_CREATE_INTEGRATION "firmir_eglfs_create_integration"
