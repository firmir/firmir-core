#pragma once

#include <firmir/render/types.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace Firmir {
namespace Render {

class RenderBuffer {
public:
    virtual ~RenderBuffer() = default;
    virtual BufferUsage usage() const = 0;
    virtual std::uint64_t size() const = 0;
    /// Uploads bytes into the buffer; returns false on overflow.
    virtual bool write(const void* data, std::uint64_t bytes,
                       std::uint64_t offset = 0) = 0;
};

class RenderTexture {
public:
    virtual ~RenderTexture() = default;
    virtual const TextureDesc& desc() const = 0;
    /// Uploads the full texture in the texture's native format.
    virtual bool write(const void* data, std::uint64_t bytes) = 0;
};

class RenderShader {
public:
    virtual ~RenderShader() = default;
    virtual ShaderStage stage() const = 0;
    virtual const std::string& entryPoint() const = 0;
};

class RenderPipeline {
public:
    virtual ~RenderPipeline() = default;
    virtual const PipelineDesc& desc() const = 0;
};

class RenderTarget {
public:
    virtual ~RenderTarget() = default;
    virtual Size size() const = 0;
    virtual TextureFormat colorFormat() const = 0;
    virtual bool hasDepth() const = 0;
};

class RenderSwapchain {
public:
    virtual ~RenderSwapchain() = default;
    virtual bool acquire() = 0;
    virtual RenderTarget* target() = 0;
    virtual bool present() = 0;
    virtual bool resize(int width, int height) = 0;
};

/**
 * @brief Optional RenderTarget extension for CPU-backed targets.
 *
 * Software backends keep the color buffer in system memory; dynamic_cast a
 * RenderTarget* to CpuRenderTarget to read the pixels back without a copy
 * (e.g. to hand them to a display backend). Pixels are 0xAARRGGBB words,
 * row-major, stride == size().width, and stay valid until the next submit()
 * that touches the target.
 */
class CpuRenderTarget : public RenderTarget {
public:
    virtual const std::uint32_t* pixels() const = 0;
};

/**
 * @brief Recorded command list for one or more render passes.
 *
 * Commands must be recorded between begin() and end(); draw calls must be
 * issued between beginRenderPass() and endRenderPass().
 */
class RenderCommandBuffer {
public:
    virtual ~RenderCommandBuffer() = default;
    virtual void begin() = 0;
    virtual void end() = 0;

    virtual void beginRenderPass(RenderTarget* target, const ClearValue& clear) = 0;
    virtual void endRenderPass() = 0;

    virtual void setViewport(const Viewport& viewport) = 0;
    virtual void setScissor(const Rect& rect) = 0;
    virtual void bindPipeline(RenderPipeline* pipeline) = 0;
    virtual void bindVertexBuffer(std::uint32_t slot, RenderBuffer* buffer,
                                  std::uint64_t offset = 0) = 0;
    virtual void bindIndexBuffer(RenderBuffer* buffer, IndexFormat format,
                                 std::uint64_t offset = 0) = 0;
    virtual void bindUniformBuffer(std::uint32_t slot, RenderBuffer* buffer,
                                   std::uint64_t offset = 0,
                                   std::uint64_t bytes = 0) = 0;
    virtual void bindTexture(std::uint32_t slot, RenderTexture* texture) = 0;

    virtual void draw(std::uint32_t vertexCount,
                      std::uint32_t firstVertex = 0) = 0;
    virtual void drawIndexed(std::uint32_t indexCount,
                             std::uint32_t firstIndex = 0,
                             std::int32_t vertexOffset = 0) = 0;
};

/**
 * @brief GPU device/context abstraction.
 *
 * Created by a backend plugin (see backend.hpp). All created objects are
 * owned by the caller and must be destroyed before the device.
 */
class RenderDevice {
public:
    virtual ~RenderDevice() = default;
    virtual std::string backendName() const = 0;

    virtual RenderBuffer* createBuffer(const BufferDesc& desc) = 0;
    virtual RenderTexture* createTexture(const TextureDesc& desc) = 0;
    virtual RenderShader* createShader(const ShaderDesc& desc) = 0;
    virtual RenderPipeline* createPipeline(const PipelineDesc& desc,
                                           RenderShader* vertex,
                                           RenderShader* fragment) = 0;
    virtual RenderTarget* createRenderTarget(const RenderTargetDesc& desc) = 0;
    virtual RenderSwapchain* createSwapchain(const SwapchainDesc& desc) = 0;
    virtual RenderCommandBuffer* createCommandBuffer() = 0;

    virtual void submit(RenderCommandBuffer* commands) = 0;
    virtual bool present(RenderSwapchain* swapchain) = 0;
    virtual void waitIdle() = 0;
};

} // namespace Render
} // namespace Firmir
