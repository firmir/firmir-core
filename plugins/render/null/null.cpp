// Null firmir-render backend: validates API flow and records commands without
// a GPU. Used by tests and as the default fallback backend.

#include <firmir/render/backend.hpp>
#include <null_control.hpp>

#include <cstring>
#include <memory>
#include <utility>
#include <vector>

using namespace Firmir::Render;

namespace Firmir {
namespace Render {
namespace Null {

Stats& stats() {
    static Stats value;
    return value;
}

void reset() { stats() = Stats{}; }

} // namespace Null
} // namespace Render
} // namespace Firmir

namespace {

int bytesPerPixel(TextureFormat format) {
    switch (format) {
    case TextureFormat::Rgba8:
    case TextureFormat::Bgra8:
        return 4;
    case TextureFormat::Depth24Stencil8:
        return 4;
    }
    return 4;
}

class NullBuffer : public RenderBuffer {
public:
    explicit NullBuffer(const BufferDesc& desc) : m_desc(desc), m_data(desc.size) {
        Null::stats().buffers++;
    }

    BufferUsage usage() const override { return m_desc.usage; }
    std::uint64_t size() const override { return m_desc.size; }
    bool write(const void* data, std::uint64_t bytes,
               std::uint64_t offset) override {
        if (offset > m_desc.size || bytes > m_desc.size - offset ||
            (bytes != 0 && !data))
            return false;
        if (bytes != 0)
            std::memcpy(m_data.data() + offset, data, static_cast<std::size_t>(bytes));
        Null::stats().bytesWritten += bytes;
        return true;
    }

private:
    BufferDesc m_desc;
    std::vector<std::uint8_t> m_data;
};

class NullTexture : public RenderTexture {
public:
    explicit NullTexture(const TextureDesc& desc) : m_desc(desc) {
        Null::stats().textures++;
    }

    const TextureDesc& desc() const override { return m_desc; }
    bool write(const void* data, std::uint64_t bytes) override {
        const std::uint64_t expected =
            static_cast<std::uint64_t>(m_desc.width) * m_desc.height *
            bytesPerPixel(m_desc.format);
        if (!data || bytes != expected)
            return false;
        Null::stats().bytesWritten += bytes;
        return true;
    }

private:
    TextureDesc m_desc;
};

class NullShader : public RenderShader {
public:
    explicit NullShader(const ShaderDesc& desc)
        : m_stage(desc.stage), m_entryPoint(desc.entryPoint) {
        Null::stats().shaders++;
    }

    ShaderStage stage() const override { return m_stage; }
    const std::string& entryPoint() const override { return m_entryPoint; }

private:
    ShaderStage m_stage;
    std::string m_entryPoint;
};

class NullPipeline : public RenderPipeline {
public:
    explicit NullPipeline(const PipelineDesc& desc) : m_desc(desc) {
        Null::stats().pipelines++;
    }

    const PipelineDesc& desc() const override { return m_desc; }

private:
    PipelineDesc m_desc;
};

class NullTarget : public RenderTarget {
public:
    explicit NullTarget(const RenderTargetDesc& desc) : m_desc(desc) {
        Null::stats().renderTargets++;
    }

    Size size() const override { return {m_desc.width, m_desc.height}; }
    TextureFormat colorFormat() const override { return m_desc.colorFormat; }
    bool hasDepth() const override { return m_desc.depth; }

private:
    RenderTargetDesc m_desc;
};

class NullCommandBuffer : public RenderCommandBuffer {
public:
    NullCommandBuffer() { Null::stats().commandBuffers++; }

    void begin() override { m_recording = true; }
    void end() override { m_recording = false; }

    void beginRenderPass(RenderTarget* target, const ClearValue&) override {
        if (!m_recording || m_inPass || !target)
            return;
        m_inPass = true;
        Null::stats().renderPasses++;
    }
    void endRenderPass() override { m_inPass = false; }

    void setViewport(const Viewport&) override {}
    void setScissor(const Rect&) override {}
    void bindPipeline(RenderPipeline*) override {}
    void bindVertexBuffer(std::uint32_t, RenderBuffer*, std::uint64_t) override {}
    void bindIndexBuffer(RenderBuffer*, IndexFormat, std::uint64_t) override {}
    void bindUniformBuffer(std::uint32_t, RenderBuffer*, std::uint64_t,
                           std::uint64_t) override {}
    void bindTexture(std::uint32_t, RenderTexture*) override {}

    void draw(std::uint32_t vertexCount, std::uint32_t) override {
        if (!m_inPass)
            return;
        Null::stats().draws++;
        Null::stats().vertices += vertexCount;
    }
    void drawIndexed(std::uint32_t indexCount, std::uint32_t,
                     std::int32_t) override {
        if (!m_inPass)
            return;
        Null::stats().indexedDraws++;
        Null::stats().indices += indexCount;
    }

private:
    bool m_recording = false;
    bool m_inPass = false;
};

class NullSwapchain;

class NullDevice : public RenderDevice {
public:
    explicit NullDevice(const DeviceDesc& desc) : m_desc(desc) {
        Null::stats().devices++;
    }

    std::string backendName() const override { return "null"; }

    RenderBuffer* createBuffer(const BufferDesc& desc) override {
        return new NullBuffer(desc);
    }
    RenderTexture* createTexture(const TextureDesc& desc) override {
        return new NullTexture(desc);
    }
    RenderShader* createShader(const ShaderDesc& desc) override {
        return new NullShader(desc);
    }
    RenderPipeline* createPipeline(const PipelineDesc& desc,
                                   RenderShader* vertex,
                                   RenderShader* fragment) override {
        if (!vertex || !fragment || vertex->stage() != ShaderStage::Vertex ||
            fragment->stage() != ShaderStage::Fragment)
            return nullptr;
        return new NullPipeline(desc);
    }
    RenderTarget* createRenderTarget(const RenderTargetDesc& desc) override {
        return new NullTarget(desc);
    }
    RenderSwapchain* createSwapchain(const SwapchainDesc& desc) override;
    RenderCommandBuffer* createCommandBuffer() override {
        return new NullCommandBuffer;
    }

    void submit(RenderCommandBuffer* commands) override {
        if (commands)
            Null::stats().submissions++;
    }
    bool present(RenderSwapchain* swapchain) override {
        if (!swapchain)
            return false;
        Null::stats().presents++;
        return true;
    }
    void waitIdle() override {}

private:
    DeviceDesc m_desc;
};

class NullSwapchain : public RenderSwapchain {
public:
    NullSwapchain(NullDevice* device, const SwapchainDesc& desc)
        : m_device(device), m_desc(desc) {
        RenderTargetDesc targetDesc;
        targetDesc.width = desc.width;
        targetDesc.height = desc.height;
        targetDesc.colorFormat = TextureFormat::Rgba8;
        targetDesc.depth = false;
        m_target = std::make_unique<NullTarget>(targetDesc);
        Null::stats().swapchains++;
    }

    bool acquire() override { return m_device != nullptr; }
    RenderTarget* target() override { return m_target.get(); }
    bool present() override { return m_device && m_device->present(this); }
    bool resize(int width, int height) override {
        if (width <= 0 || height <= 0)
            return false;
        m_desc.width = width;
        m_desc.height = height;
        RenderTargetDesc targetDesc;
        targetDesc.width = width;
        targetDesc.height = height;
        targetDesc.colorFormat = TextureFormat::Rgba8;
        targetDesc.depth = false;
        m_target = std::make_unique<NullTarget>(targetDesc);
        return true;
    }

private:
    NullDevice* m_device;
    SwapchainDesc m_desc;
    std::unique_ptr<NullTarget> m_target;
};

RenderSwapchain* NullDevice::createSwapchain(const SwapchainDesc& desc) {
    if (desc.width <= 0 || desc.height <= 0)
        return nullptr;
    return new NullSwapchain(this, desc);
}

class NullBackend : public RenderBackend {
public:
    static NullBackend& instance() {
        static NullBackend backend;
        return backend;
    }

    std::string name() const override { return "null"; }
    RenderDevice* createDevice(const DeviceDesc& desc) override {
        return new NullDevice(desc);
    }
};

} // namespace

extern "C" Firmir::Render::RenderBackend* firmir_render_create_backend() {
    return &NullBackend::instance();
}

