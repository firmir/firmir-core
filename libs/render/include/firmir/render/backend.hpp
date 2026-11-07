#pragma once

// Plugin ABI for firmir-render backends. The base library never links against
// concrete backends: they are discovered as `firmir-render-<name>.so` modules
// in the directories listed in FIRMIR_RENDER_PLUGIN_PATH (colon-separated)
// and selected with FIRMIR_RENDER_BACKEND or Render::createDevice(name).

#include <firmir/render/device.hpp>

#include <string>

namespace Firmir {
namespace Render {

class RenderBackend {
public:
    virtual ~RenderBackend() = default;
    virtual std::string name() const = 0;
    /// Creates a device. The caller owns the returned device; the backend
    /// object itself is owned by the plugin and must outlive the device.
    virtual RenderDevice* createDevice(const DeviceDesc& desc) = 0;
};

using CreateRenderBackendFunc = RenderBackend* (*)();

} // namespace Render
} // namespace Firmir

/// Symbol a render plugin exports: `extern "C" Firmir::Render::RenderBackend*
/// firmir_render_create_backend()`.
#define FIRMIR_RENDER_CREATE_BACKEND "firmir_render_create_backend"
