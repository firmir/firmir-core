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
 * then FIRMIR_RENDER_BACKEND, then "null". Plugins are searched in
 * FIRMIR_RENDER_PLUGIN_PATH and in `plugins/render` directories relative to
 * the running executable.
 */
class Render {
public:
    /// Loads (and caches) a backend plugin; returns nullptr on failure.
    static RenderBackend* backend(const std::string& name);
    /// Creates a device from a backend plugin; returns nullptr on failure.
    static std::unique_ptr<RenderDevice> createDevice(
        const std::string& name = std::string(),
        const DeviceDesc& desc = {});
};

} // namespace Render
} // namespace Firmir
