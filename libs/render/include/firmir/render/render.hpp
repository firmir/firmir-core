#pragma once

#include <firmir/render/backend.hpp>
#include <firmir/render/device.hpp>
#include <firmir/render/types.hpp>

#include <memory>
#include <string>

namespace Firmir {
namespace Render {

/**
 * @brief Entry point for loading render backends and creating devices.
 *
 * Backend selection order for createDevice(): the explicit @p name argument,
 * then FIRMIR_RENDER_BACKEND, otherwise the first plugin of "vulkan",
 * "opengl", "soft" that both loads and creates a device - a plugin that loads
 * but cannot initialise (e.g. OpenGL without a usable EGL display) is skipped.
 * Plugins are searched in FIRMIR_RENDER_PLUGIN_PATH, in `plugins/render`
 * directories relative to the running executable and in the system plugin
 * directory.
 */
class Render {
public:
    /// Loads (and caches) a backend plugin; returns nullptr on failure.
    static RenderBackend* backend(const std::string& name);
    /// Creates a device from a backend plugin; returns nullptr on failure.
    static std::unique_ptr<RenderDevice> createDevice(
        const std::string& name = std::string(),
        const DeviceDesc& desc = {});
    /// Unloads cached plugins. All devices and their resources must already
    /// be destroyed; subsequent backend()/createDevice() calls reload them.
    static void shutdown();
};

} // namespace Render
} // namespace Firmir
