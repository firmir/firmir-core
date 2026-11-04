// Software firmir-render backend ("soft"): executes the recorded command
// stream with a CPU triangle rasterizer. firmir-gui selects it by default
// (FIRMIR_RENDER_BACKEND=soft) so the item scene graph renders through the
// firmir-render API without a GPU.
//
// Fixed-function shading convention (shader bytecode is accepted but never
// executed):
//  - location 0: vertex position, Float2 or Float3, in target pixel
//    coordinates, origin at the top-left corner (pre-transformed);
//  - location 1: vertex color, Float4 or UByte4Norm, linear 0..1;
//  - location 2 (optional): texture UV, Float2; when a texture is bound at
//    slot 0 the nearest texel is multiplied with the interpolated color.
// Only triangle lists are rasterized; other topologies are ignored.
// createSwapchain() is unsupported (returns nullptr): render into a target
// and read the pixels through the CpuRenderTarget extension instead.

#include <firmir/render/backend.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace Firmir::Render;

namespace {

std::uint8_t toByte(float value) {
    return static_cast<std::uint8_t>(
        std::clamp(std::lround(value * 255.0f), 0l, 255l));
}

std::uint32_t packARGB(float r, float g, float b, float a) {
    return (static_cast<std::uint32_t>(toByte(a)) << 24) |
           (static_cast<std::uint32_t>(toByte(r)) << 16) |
           (static_cast<std::uint32_t>(toByte(g)) << 8) | toByte(b);
}

class SoftBuffer : public RenderBuffer {
public:
    explicit SoftBuffer(const BufferDesc& desc)
        : m_desc(desc), m_data(desc.size) {}

    BufferUsage usage() const override { return m_desc.usage; }
    std::uint64_t size() const override { return m_desc.size; }
    bool write(const void* data, std::uint64_t bytes,
               std::uint64_t offset) override {
        if (offset > m_desc.size || bytes > m_desc.size - offset ||
            (bytes != 0 && !data))
            return false;
        if (bytes != 0)
            std::memcpy(m_data.data() + offset, data,
                        static_cast<std::size_t>(bytes));
        return true;
    }

    const std::uint8_t* bytes() const { return m_data.data(); }

private:
    BufferDesc m_desc;
    std::vector<std::uint8_t> m_data;
};

class SoftTexture : public RenderTexture {
public:
    explicit SoftTexture(const TextureDesc& desc)
        : m_desc(desc),
          m_pixels(static_cast<std::size_t>(desc.width) * desc.height, 0) {}

    const TextureDesc& desc() const override { return m_desc; }
    bool write(const void* data, std::uint64_t bytes) override {
        const std::uint64_t expected =
            static_cast<std::uint64_t>(m_desc.width) * m_desc.height * 4;
        if (!data || bytes != expected)
            return false;
        const auto* src = static_cast<const std::uint8_t*>(data);
        const bool bgra = m_desc.format == TextureFormat::Bgra8;
        for (std::size_t i = 0; i < m_pixels.size(); ++i) {
            const std::uint8_t c0 = src[i * 4 + 0];
            const std::uint8_t c1 = src[i * 4 + 1];
            const std::uint8_t c2 = src[i * 4 + 2];
            const std::uint8_t a = src[i * 4 + 3];
            const std::uint8_t r = bgra ? c2 : c0;
            const std::uint8_t b = bgra ? c0 : c2;
            m_pixels[i] = (static_cast<std::uint32_t>(a) << 24) |
                          (static_cast<std::uint32_t>(r) << 16) |
                          (static_cast<std::uint32_t>(c1) << 8) | b;
        }
        return true;
    }

    /// Nearest-neighbour sample, uv in [0, 1] (clamped); 0xAARRGGBB.
    std::uint32_t sample(float u, float v) const {
        const int x = std::clamp(static_cast<int>(u * m_desc.width), 0,
                                 m_desc.width - 1);
        const int y = std::clamp(static_cast<int>(v * m_desc.height), 0,
                                 m_desc.height - 1);
        return m_pixels[static_cast<std::size_t>(y) * m_desc.width + x];
    }

private:
    TextureDesc m_desc;
    std::vector<std::uint32_t> m_pixels;
};

class SoftShader : public RenderShader {
public:
    explicit SoftShader(const ShaderDesc& desc)
        : m_stage(desc.stage), m_entryPoint(desc.entryPoint) {}

    ShaderStage stage() const override { return m_stage; }
    const std::string& entryPoint() const override { return m_entryPoint; }

private:
    ShaderStage m_stage;
    std::string m_entryPoint;
};

class SoftPipeline : public RenderPipeline {
public:
    explicit SoftPipeline(const PipelineDesc& desc) : m_desc(desc) {}
    const PipelineDesc& desc() const override { return m_desc; }

private:
    PipelineDesc m_desc;
};

class SoftTarget : public CpuRenderTarget {
public:
    explicit SoftTarget(const RenderTargetDesc& desc)
        : m_desc(desc),
          m_pixels(static_cast<std::size_t>(desc.width) * desc.height, 0) {}

    Size size() const override { return {m_desc.width, m_desc.height}; }
    TextureFormat colorFormat() const override { return m_desc.colorFormat; }
    bool hasDepth() const override { return m_desc.depth; }
    const std::uint32_t* pixels() const override { return m_pixels.data(); }

    void clear(std::uint32_t argb) {
        std::fill(m_pixels.begin(), m_pixels.end(), argb);
    }
    std::uint32_t& at(int x, int y) {
        return m_pixels[static_cast<std::size_t>(y) * m_desc.width + x];
    }

private:
    RenderTargetDesc m_desc;
    std::vector<std::uint32_t> m_pixels;
};

/// Decoded vertex in the fixed-function layout (see the file header).
struct Vertex {
    float x = 0, y = 0;
    float r = 1, g = 1, b = 1, a = 1;
    float u = 0, v = 0;
};

struct ExecState {
    SoftTarget* target = nullptr;
    Rect scissor{0, 0, 0, 0};
    const SoftPipeline* pipeline = nullptr;
    const SoftBuffer* vertexBuffer = nullptr;
    std::uint64_t vertexOffset = 0;
    const SoftBuffer* indexBuffer = nullptr;
    IndexFormat indexFormat = IndexFormat::UInt16;
    std::uint64_t indexOffset = 0;
    const SoftTexture* texture = nullptr; // slot 0
};

/// Edge function for the edge a->b evaluated at p; positive on the interior
/// side for positive-area triangles.
float edge(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/// D3D-style top-left rule: with the interior on the positive edge-function
/// side, an edge owns its tie pixels when it is a top edge (horizontal,
/// going right) or a left edge (going up).
bool isTopLeft(float ax, float ay, float bx, float by) {
    return (ay == by && ax < bx) || ay > by;
}

Rect rectIntersect(const Rect& a, const Rect& b) {
    const int nx = std::max(a.x, b.x);
    const int ny = std::max(a.y, b.y);
    const int nx2 = std::min(a.x + a.width, b.x + b.width);
    const int ny2 = std::min(a.y + a.height, b.y + b.height);
    if (nx2 <= nx || ny2 <= ny)
        return {};
    return {nx, ny, nx2 - nx, ny2 - ny};
}

void writePixel(SoftTarget& target, int x, int y, float r, float g, float b,
                float a, const BlendState& blend) {
    std::uint32_t& dst = target.at(x, y);
    const std::uint8_t sr = toByte(r);
    const std::uint8_t sg = toByte(g);
    const std::uint8_t sb = toByte(b);
    const unsigned sa = toByte(a);
    if (!blend.enabled) {
        dst = (static_cast<std::uint32_t>(sa) << 24) |
              (static_cast<std::uint32_t>(sr) << 16) |
              (static_cast<std::uint32_t>(sg) << 8) | sb;
        return;
    }
    // Source-over (SrcAlpha / OneMinusSrcAlpha), the GUI path: legacy
    // integer math, kept bit-exact with the pre-render Painter rasterizer.
    if (blend.srcColor == BlendFactor::SrcAlpha &&
        blend.dstColor == BlendFactor::OneMinusSrcAlpha) {
        const unsigned da = (dst >> 24) & 0xff;
        const unsigned dr = (dst >> 16) & 0xff;
        const unsigned dg = (dst >> 8) & 0xff;
        const unsigned db = dst & 0xff;
        const unsigned ia = 255 - sa;
        const unsigned outA = sa + da * ia / 255;
        const unsigned outR = (sr * sa + dr * ia) / 255;
        const unsigned outG = (sg * sa + dg * ia) / 255;
        const unsigned outB = (sb * sa + db * ia) / 255;
        dst = (outA << 24) | (outR << 16) | (outG << 8) | outB;
        return;
    }
    // Generic fallback: src*srcFactor + dst*dstFactor per channel (float).
    auto factor = [](BlendFactor f, float srcA, float dstA) {
        switch (f) {
        case BlendFactor::Zero: return 0.0f;
        case BlendFactor::One: return 1.0f;
        case BlendFactor::SrcAlpha: return srcA;
        case BlendFactor::OneMinusSrcAlpha: return 1.0f - srcA;
        case BlendFactor::DstAlpha: return dstA;
        case BlendFactor::OneMinusDstAlpha: return 1.0f - dstA;
        }
        return 1.0f;
    };
    const float da = ((dst >> 24) & 0xff) / 255.0f;
    const float sf = factor(blend.srcColor, a, da);
    const float df = factor(blend.dstColor, a, da);
    const float outR = r * sf + (((dst >> 16) & 0xff) / 255.0f) * df;
    const float outG = g * sf + (((dst >> 8) & 0xff) / 255.0f) * df;
    const float outB = b * sf + ((dst & 0xff) / 255.0f) * df;
    const float outA = a * sf + da * df;
    dst = packARGB(outR, outG, outB, outA);
}

Vertex fetchVertex(const ExecState& state, std::uint32_t index) {
    Vertex out;
    const VertexLayout& layout = state.pipeline->desc().vertexLayout;
    const std::uint8_t* base =
        state.vertexBuffer->bytes() + state.vertexOffset +
        static_cast<std::uint64_t>(index) * layout.stride;
    for (const VertexAttribute& attr : layout.attributes) {
        const std::uint8_t* p = base + attr.offset;
        switch (attr.location) {
        case 0: // position
            if (attr.format == VertexFormat::Float2 ||
                attr.format == VertexFormat::Float3) {
                std::memcpy(&out.x, p, sizeof(float));
                std::memcpy(&out.y, p + sizeof(float), sizeof(float));
            }
            break;
        case 1: // color
            if (attr.format == VertexFormat::Float4) {
                std::memcpy(&out.r, p, sizeof(float));
                std::memcpy(&out.g, p + 4, sizeof(float));
                std::memcpy(&out.b, p + 8, sizeof(float));
                std::memcpy(&out.a, p + 12, sizeof(float));
            } else if (attr.format == VertexFormat::UByte4Norm) {
                out.r = p[0] / 255.0f;
                out.g = p[1] / 255.0f;
                out.b = p[2] / 255.0f;
                out.a = p[3] / 255.0f;
            }
            break;
        case 2: // uv
            if (attr.format == VertexFormat::Float2) {
                std::memcpy(&out.u, p, sizeof(float));
                std::memcpy(&out.v, p + sizeof(float), sizeof(float));
            }
            break;
        default:
            break;
        }
    }
    return out;
}

void rasterizeTriangle(ExecState& state, Vertex v0, Vertex v1, Vertex v2) {
    SoftTarget& target = *state.target;
    const Size size = target.size();
    float area = edge(v0.x, v0.y, v1.x, v1.y, v2.x, v2.y);
    if (area == 0.0f)
        return;
    if (area < 0.0f) { // enforce positive area (interior on the + side)
        std::swap(v1, v2);
        area = -area;
    }
    const int minX = static_cast<int>(std::floor(std::min({v0.x, v1.x, v2.x})));
    const int minY = static_cast<int>(std::floor(std::min({v0.y, v1.y, v2.y})));
    const Rect bounds{minX, minY,
                      static_cast<int>(std::ceil(std::max({v0.x, v1.x, v2.x}))) -
                          minX,
                      static_cast<int>(std::ceil(std::max({v0.y, v1.y, v2.y}))) -
                          minY};
    Rect clip{0, 0, size.width, size.height};
    if (state.pipeline->desc().rasterizer.scissorEnabled)
        clip = rectIntersect(clip, state.scissor);
    const Rect region = rectIntersect(bounds, clip);
    const bool textured = state.texture != nullptr;
    const BlendState& blend = state.pipeline->desc().blend;
    for (int py = region.y; py < region.y + region.height; ++py) {
        for (int px = region.x; px < region.x + region.width; ++px) {
            const float cx = px + 0.5f;
            const float cy = py + 0.5f;
            const float e0 = edge(v1.x, v1.y, v2.x, v2.y, cx, cy);
            const float e1 = edge(v2.x, v2.y, v0.x, v0.y, cx, cy);
            const float e2 = edge(v0.x, v0.y, v1.x, v1.y, cx, cy);
            const bool in0 =
                e0 > 0 || (e0 == 0 && isTopLeft(v1.x, v1.y, v2.x, v2.y));
            const bool in1 =
                e1 > 0 || (e1 == 0 && isTopLeft(v2.x, v2.y, v0.x, v0.y));
            const bool in2 =
                e2 > 0 || (e2 == 0 && isTopLeft(v0.x, v0.y, v1.x, v1.y));
            if (!in0 || !in1 || !in2)
                continue;
            const float w0 = e0 / area;
            const float w1 = e1 / area;
            const float w2 = 1.0f - w0 - w1;
            float r = v0.r * w0 + v1.r * w1 + v2.r * w2;
            float g = v0.g * w0 + v1.g * w1 + v2.g * w2;
            float b = v0.b * w0 + v1.b * w1 + v2.b * w2;
            float a = v0.a * w0 + v1.a * w1 + v2.a * w2;
            if (textured) {
                const float u = v0.u * w0 + v1.u * w1 + v2.u * w2;
                const float v = v0.v * w0 + v1.v * w1 + v2.v * w2;
                const std::uint32_t texel = state.texture->sample(u, v);
                r *= ((texel >> 16) & 0xff) / 255.0f;
                g *= ((texel >> 8) & 0xff) / 255.0f;
                b *= (texel & 0xff) / 255.0f;
                a *= ((texel >> 24) & 0xff) / 255.0f;
            }
            writePixel(target, px, py, r, g, b, a, blend);
        }
    }
}

std::uint32_t fetchIndex(const ExecState& state, std::uint32_t i) {
    const std::uint8_t* base = state.indexBuffer->bytes() + state.indexOffset;
    if (state.indexFormat == IndexFormat::UInt16) {
        std::uint16_t value;
        std::memcpy(&value, base + static_cast<std::uint64_t>(i) * 2, 2);
        return value;
    }
    std::uint32_t value;
    std::memcpy(&value, base + static_cast<std::uint64_t>(i) * 4, 4);
    return value;
}

void drawTriangles(ExecState& state, const std::vector<std::uint32_t>& idx) {
    if (!state.pipeline || !state.vertexBuffer || !state.target)
        return;
    if (state.pipeline->desc().topology != PrimitiveTopology::TriangleList)
        return;
    for (std::size_t i = 0; i + 2 < idx.size(); i += 3)
        rasterizeTriangle(state, fetchVertex(state, idx[i]),
                          fetchVertex(state, idx[i + 1]),
                          fetchVertex(state, idx[i + 2]));
}

struct Command {
    enum Kind {
        BeginPass,
        EndPass,
        Scissor,
        BindPipeline,
        BindVertexBuffer,
        BindIndexBuffer,
        BindTexture,
        Draw,
        DrawIndexed,
    } kind;

    explicit Command(Kind k) : kind(k) {}

    SoftTarget* target = nullptr;
    ClearValue clear;
    Rect rect{};
    const SoftPipeline* pipeline = nullptr;
    const SoftBuffer* buffer = nullptr;
    std::uint64_t offset = 0;
    IndexFormat indexFormat = IndexFormat::UInt16;
    const SoftTexture* texture = nullptr;
    std::uint32_t count = 0;
    std::uint32_t first = 0;
    std::int32_t vertexOffset = 0;
};

class SoftCommandBuffer : public RenderCommandBuffer {
public:
    void begin() override {
        m_commands.clear();
        m_recording = true;
    }
    void end() override { m_recording = false; }

    void beginRenderPass(RenderTarget* target,
                         const ClearValue& clear) override {
        if (!m_recording)
            return;
        Command cmd{Command::BeginPass};
        cmd.target = static_cast<SoftTarget*>(target);
        cmd.clear = clear;
        m_commands.push_back(cmd);
    }
    void endRenderPass() override {
        if (!m_recording)
            return;
        m_commands.push_back(Command{Command::EndPass});
    }
    void setViewport(const Viewport&) override {}
    void setScissor(const Rect& rect) override {
        Command cmd{Command::Scissor};
        cmd.rect = rect;
        m_commands.push_back(cmd);
    }
    void bindPipeline(RenderPipeline* pipeline) override {
        Command cmd{Command::BindPipeline};
        cmd.pipeline = static_cast<const SoftPipeline*>(pipeline);
        m_commands.push_back(cmd);
    }
    void bindVertexBuffer(std::uint32_t slot, RenderBuffer* buffer,
                          std::uint64_t offset) override {
        if (slot != 0)
            return;
        Command cmd{Command::BindVertexBuffer};
        cmd.buffer = static_cast<const SoftBuffer*>(buffer);
        cmd.offset = offset;
        m_commands.push_back(cmd);
    }
    void bindIndexBuffer(RenderBuffer* buffer, IndexFormat format,
                         std::uint64_t offset) override {
        Command cmd{Command::BindIndexBuffer};
        cmd.buffer = static_cast<const SoftBuffer*>(buffer);
        cmd.indexFormat = format;
        cmd.offset = offset;
        m_commands.push_back(cmd);
    }
    void bindUniformBuffer(std::uint32_t, RenderBuffer*, std::uint64_t,
                           std::uint64_t) override {
        // Fixed-function shading: no uniform buffers.
    }
    void bindTexture(std::uint32_t slot, RenderTexture* texture) override {
        if (slot != 0)
            return;
        Command cmd{Command::BindTexture};
        cmd.texture = static_cast<const SoftTexture*>(texture);
        m_commands.push_back(cmd);
    }
    void draw(std::uint32_t vertexCount, std::uint32_t firstVertex) override {
        Command cmd{Command::Draw};
        cmd.count = vertexCount;
        cmd.first = firstVertex;
        m_commands.push_back(cmd);
    }
    void drawIndexed(std::uint32_t indexCount, std::uint32_t firstIndex,
                     std::int32_t vertexOffset) override {
        Command cmd{Command::DrawIndexed};
        cmd.count = indexCount;
        cmd.first = firstIndex;
        cmd.vertexOffset = vertexOffset;
        m_commands.push_back(cmd);
    }

    void execute() {
        ExecState state;
        for (const Command& cmd : m_commands) {
            switch (cmd.kind) {
            case Command::BeginPass:
                state.target = cmd.target;
                state.scissor = {0, 0, cmd.target->size().width,
                                 cmd.target->size().height};
                state.pipeline = nullptr;
                state.vertexBuffer = nullptr;
                state.indexBuffer = nullptr;
                state.texture = nullptr;
                cmd.target->clear(packARGB(cmd.clear.r, cmd.clear.g,
                                           cmd.clear.b, cmd.clear.a));
                break;
            case Command::EndPass:
                state.target = nullptr;
                break;
            case Command::Scissor:
                state.scissor = cmd.rect;
                break;
            case Command::BindPipeline:
                state.pipeline = cmd.pipeline;
                break;
            case Command::BindVertexBuffer:
                state.vertexBuffer = cmd.buffer;
                state.vertexOffset = cmd.offset;
                break;
            case Command::BindIndexBuffer:
                state.indexBuffer = cmd.buffer;
                state.indexFormat = cmd.indexFormat;
                state.indexOffset = cmd.offset;
                break;
            case Command::BindTexture:
                state.texture = cmd.texture;
                break;
            case Command::Draw: {
                std::vector<std::uint32_t> indices(cmd.count);
                for (std::uint32_t i = 0; i < cmd.count; ++i)
                    indices[i] = cmd.first + i;
                drawTriangles(state, indices);
                break;
            }
            case Command::DrawIndexed: {
                if (!state.indexBuffer)
                    break;
                std::vector<std::uint32_t> indices(cmd.count);
                for (std::uint32_t i = 0; i < cmd.count; ++i)
                    indices[i] = static_cast<std::uint32_t>(
                        static_cast<std::int64_t>(cmd.vertexOffset) +
                        fetchIndex(state, cmd.first + i));
                drawTriangles(state, indices);
                break;
            }
            }
        }
    }

private:
    std::vector<Command> m_commands;
    bool m_recording = false;
};

class SoftDevice : public RenderDevice {
public:
    explicit SoftDevice(const DeviceDesc& desc) : m_desc(desc) {}

    std::string backendName() const override { return "soft"; }

    RenderBuffer* createBuffer(const BufferDesc& desc) override {
        return new SoftBuffer(desc);
    }
    RenderTexture* createTexture(const TextureDesc& desc) override {
        if (desc.width <= 0 || desc.height <= 0 ||
            desc.format == TextureFormat::Depth24Stencil8)
            return nullptr;
        return new SoftTexture(desc);
    }
    RenderShader* createShader(const ShaderDesc& desc) override {
        return new SoftShader(desc);
    }
    RenderPipeline* createPipeline(const PipelineDesc& desc,
                                   RenderShader* vertex,
                                   RenderShader* fragment) override {
        if (!vertex || !fragment || vertex->stage() != ShaderStage::Vertex ||
            fragment->stage() != ShaderStage::Fragment)
            return nullptr;
        if (desc.vertexLayout.stride == 0)
            return nullptr;
        return new SoftPipeline(desc);
    }
    RenderTarget* createRenderTarget(const RenderTargetDesc& desc) override {
        if (desc.width <= 0 || desc.height <= 0 ||
            desc.colorFormat == TextureFormat::Depth24Stencil8)
            return nullptr;
        return new SoftTarget(desc);
    }
    RenderSwapchain* createSwapchain(const SwapchainDesc&) override {
        return nullptr; // render into a target + CpuRenderTarget instead
    }
    RenderCommandBuffer* createCommandBuffer() override {
        return new SoftCommandBuffer;
    }

    void submit(RenderCommandBuffer* commands) override {
        if (commands)
            static_cast<SoftCommandBuffer*>(commands)->execute();
    }
    bool present(RenderSwapchain*) override { return false; }
    void waitIdle() override {}

private:
    DeviceDesc m_desc;
};

class SoftBackend : public RenderBackend {
public:
    static SoftBackend& instance() {
        static SoftBackend backend;
        return backend;
    }

    std::string name() const override { return "soft"; }
    RenderDevice* createDevice(const DeviceDesc& desc) override {
        return new SoftDevice(desc);
    }
};

} // namespace

extern "C" Firmir::Render::RenderBackend* firmir_render_create_backend() {
    return &SoftBackend::instance();
}
