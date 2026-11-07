#include <firmir/gui/painter.hpp>

#include "detail/font8x8.h"

#include <firmir/render/device.hpp>
#ifdef FIRMIR_HAVE_SHADERC
#include <shaderc/shaderc.hpp>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace Firmir {
namespace Gui {

namespace {

unsigned parseHexByte(const std::string& text, size_t offset) {
    unsigned value = 0;
    for (size_t i = 0; i < 2; ++i) {
        const char c = text[offset + i];
        value <<= 4;
        if (c >= '0' && c <= '9')
            value |= static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f')
            value |= static_cast<unsigned>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            value |= static_cast<unsigned>(c - 'A' + 10);
        else
            throw std::invalid_argument("Color::fromString: bad hex digit");
    }
    return value;
}

std::uint8_t scaleChannel(std::uint8_t channel, double factor, bool divide) {
    const double value = divide ? channel / factor : channel * factor;
    return static_cast<std::uint8_t>(
        std::clamp(std::lround(value), 0l, 255l));
}

} // namespace

Color Color::fromString(const std::string& text) {
    if (text.size() != 7 && text.size() != 9)
        throw std::invalid_argument(
            "Color::fromString: expected '#RRGGBB' or '#AARRGGBB'");
    if (text[0] != '#')
        throw std::invalid_argument("Color::fromString: expected leading '#'");
    const size_t offset = text.size() == 9 ? 2 : 0;
    Color color;
    color.a = text.size() == 9
                  ? static_cast<std::uint8_t>(parseHexByte(text, 1))
                  : 255;
    color.r = static_cast<std::uint8_t>(parseHexByte(text, 1 + offset));
    color.g = static_cast<std::uint8_t>(parseHexByte(text, 3 + offset));
    color.b = static_cast<std::uint8_t>(parseHexByte(text, 5 + offset));
    return color;
}

Color Color::lighter(double factor) const {
    return rgba(scaleChannel(r, factor, false), scaleChannel(g, factor, false),
                scaleChannel(b, factor, false), a);
}

Color Color::darker(double factor) const {
    if (factor == 0.0)
        factor = 1.0;
    return rgba(scaleChannel(r, factor, true), scaleChannel(g, factor, true),
                scaleChannel(b, factor, true), a);
}

namespace {

using namespace Firmir::Render;

/// Solid-color quad vertex (matches the soft backend's fixed-function layout:
/// location 0 Float2 position, location 1 UByte4Norm RGBA color).
struct SolidVertex {
    float x, y;
    std::uint8_t rgba[4];
};

/// Textured quad vertex (location 2 Float2 uv appended).
struct TextVertex {
    float x, y;
    std::uint8_t rgba[4];
    float u, v;
};

std::vector<std::uint8_t> dummyShaderCode() { return {'f', 'x', 'd'}; }

Render::Rect toRenderRect(const Rect& rect) {
    return Render::Rect{rect.x, rect.y, rect.width, rect.height};
}

/// Runs @p fn for every Unicode code point in the UTF-8 string; invalid
/// sequences yield U+FFFD.
template <typename F>
void forEachCodepoint(const std::string& text, F&& fn) {
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0xFFFD;
        std::size_t len = 1;
        if (c < 0x80) {
            cp = c;
        } else if ((c & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F);
            len = 2;
        } else if ((c & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((c & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) |
                 (text[i + 2] & 0x3F);
            len = 3;
        } else if ((c & 0xF0) == 0xF0 && i + 3 < text.size()) {
            cp = ((c & 0x07) << 18) | ((text[i + 1] & 0x3F) << 12) |
                 ((text[i + 2] & 0x3F) << 6) | (text[i + 3] & 0x3F);
            len = 4;
        }
        fn(cp);
        i += len;
    }
}

} // namespace

ShaderDesc customFragmentDesc(RenderDevice* device, const std::string& body) {
    const std::string backend = device->backendName();
    if (backend == "opengl") {
        const std::string source =
            "precision mediump float;\n"
            "varying vec4 v_color; varying vec2 v_uv;\n"
            "uniform sampler2D u_texture; uniform bool u_textured;\n"
            "void main() {\n" + body + "\n}\n";
        return {ShaderStage::Fragment, ShaderLanguage::Glsl, "main",
                std::vector<std::uint8_t>(source.begin(), source.end())};
    }
#ifdef FIRMIR_HAVE_SHADERC
    if (backend == "vulkan") {
        const std::string source =
            "#version 450\n"
            "layout(location=0) in vec4 inColor; layout(location=1) in vec2 inUv;\n"
            "layout(location=0) out vec4 outColor;\n"
            "layout(set=0,binding=1) uniform sampler2D u_texture;\n"
            "layout(push_constant) uniform PushConstants { vec2 targetSize; int textured; int padding; } pc;\n"
            "#define texture2D texture\n"
            "void main() {\n"
            "vec4 v_color=inColor; vec2 v_uv=inUv; bool u_textured=pc.textured!=0;\n"
            "vec4 gl_FragColor=vec4(0.0);\n" + body + "\noutColor=gl_FragColor;\n}\n";
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
        const auto result = compiler.CompileGlslToSpv(
            source, shaderc_fragment_shader, "firmir-shader-effect.frag", options);
        if (result.GetCompilationStatus() == shaderc_compilation_status_success) {
            const std::vector<std::uint32_t> words(result.cbegin(), result.cend());
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(words.data());
            return {ShaderStage::Fragment, ShaderLanguage::Spirv, "main",
                    std::vector<std::uint8_t>(bytes, bytes + words.size() * 4)};
        }
        (void)result;
    }
#else
    (void)body;
#endif
    return {ShaderStage::Fragment, ShaderLanguage::Glsl, "main", dummyShaderCode()};
}

class RenderResources::Impl {
public:
    explicit Impl(RenderDevice* device) : device(device) {}
    RenderPipeline* pipeline(bool textured, bool blending) {
        std::unique_ptr<RenderPipeline>& slot =
            !blending ? clearPipeline_
                      : (textured ? texturedPipeline_ : solidPipeline_);
        if (!slot) {
            if (!vertexShader) {
                ShaderDesc vsDesc{ShaderStage::Vertex, ShaderLanguage::Glsl,
                                  "main", dummyShaderCode()};
                ShaderDesc fsDesc{ShaderStage::Fragment, ShaderLanguage::Glsl,
                                  "main", dummyShaderCode()};
                vertexShader.reset(device->createShader(vsDesc));
                fragmentShader.reset(device->createShader(fsDesc));
            }
            PipelineDesc desc;
            desc.topology = PrimitiveTopology::TriangleList;
            desc.rasterizer.scissorEnabled = true;
            desc.blend.enabled = blending;
            desc.blend.srcColor = BlendFactor::SrcAlpha;
            desc.blend.dstColor = BlendFactor::OneMinusSrcAlpha;
            if (textured) {
                desc.vertexLayout.stride = sizeof(TextVertex);
                desc.vertexLayout.attributes = {
                    {0, "pos", VertexFormat::Float2, 0},
                    {1, "color", VertexFormat::UByte4Norm, 8},
                    {2, "uv", VertexFormat::Float2, 12},
                };
            } else {
                desc.vertexLayout.stride = sizeof(SolidVertex);
                desc.vertexLayout.attributes = {
                    {0, "pos", VertexFormat::Float2, 0},
                    {1, "color", VertexFormat::UByte4Norm, 8},
                };
            }
            slot.reset(device->createPipeline(desc, vertexShader.get(),
                                              fragmentShader.get()));
        }
        return slot.get();
    }


    RenderTexture* fontAtlas() {
        if (!fontAtlas_) {
            TextureDesc desc;
            desc.width = 128 * 8;
            desc.height = 8;
            desc.format = TextureFormat::Rgba8;
            fontAtlas_.reset(device->createTexture(desc));
            if (fontAtlas_) {
                std::vector<std::uint8_t> bytes(
                    static_cast<std::size_t>(desc.width) * desc.height * 4, 0);
                for (int glyph = 0; glyph < 128; ++glyph)
                    for (int row = 0; row < 8; ++row) {
                        const unsigned char bits = font8x8_basic[glyph][row];
                        for (int col = 0; col < 8; ++col) {
                            if (!((bits >> col) & 1))
                                continue;
                            const std::size_t px =
                                (static_cast<std::size_t>(row) * desc.width +
                                 glyph * 8 + col) * 4;
                            bytes[px + 0] = 255;
                            bytes[px + 1] = 255;
                            bytes[px + 2] = 255;
                            bytes[px + 3] = 255;
                        }
                    }
                fontAtlas_->write(bytes.data(), bytes.size());
            }
        }
        return fontAtlas_.get();
    }


    /// Vertex storage for a quad written at @p offset within the current
    /// frame. Grown buffers are retired (kept alive) because draws recorded
    /// earlier in the pass still reference them; the retire list is reset
    /// when a new frame starts (previous submission has completed).
    RenderBuffer* vertexBuffer(std::uint64_t offset, std::uint64_t bytes) {
        if (offset == 0)
            retiredBuffers_.clear();
        if (!vertexBuffer_ || vertexBuffer_->size() < offset + bytes) {
            const std::uint64_t current =
                vertexBuffer_ ? vertexBuffer_->size() : 0;
            BufferDesc desc{BufferUsage::Vertex,
                            std::max({std::uint64_t(4096), current * 2,
                                      offset + bytes}),
                            true};
            if (vertexBuffer_)
                retiredBuffers_.push_back(std::move(vertexBuffer_));
            vertexBuffer_.reset(device->createBuffer(desc));
        }
        return vertexBuffer_.get();
    }

    RenderTexture* rgbaTexture(const void* key, std::uint64_t generation,
                               int width, int height, const void* pixels) {
        if (!key || !pixels || width <= 0 || height <= 0)
            return nullptr;
        auto& entry = rgbaTextures_[key];
        const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
        if (!entry.texture || entry.width != width || entry.height != height) {
            TextureDesc desc;
            desc.width = width;
            desc.height = height;
            desc.sampler.minFilter = Filter::Linear;
            desc.sampler.magFilter = Filter::Linear;
            entry.texture.reset(device->createTexture(desc));
            entry.generation = generation - 1;
        }
        if (!entry.texture)
            return nullptr;
        if (entry.generation != generation) {
            if (!entry.texture->write(pixels, bytes))
                return nullptr;
            entry.generation = generation;
            entry.width = width;
            entry.height = height;
        }
        return entry.texture.get();
    }

    RenderPipeline* customPipeline(const void* key, const std::string& body) {
        RenderPipeline* fallback = pipeline(true, true);
        if (!key || device->backendName() == "soft" || device->backendName() == "null")
            return fallback;
        auto& entry = shaderEffects_[key];
        if (entry.pipeline && entry.body == body)
            return entry.pipeline.get();
        entry.body = body;
        entry.fragment.reset(device->createShader(customFragmentDesc(device, body)));
        PipelineDesc desc;
        desc.vertexLayout.stride = sizeof(TextVertex);
        desc.vertexLayout.attributes = {
            {0, "pos", VertexFormat::Float2, 0},
            {1, "color", VertexFormat::UByte4Norm, 8},
            {2, "uv", VertexFormat::Float2, 12},
        };
        desc.rasterizer.scissorEnabled = true;
        desc.blend.enabled = true;
        desc.blend.srcColor = BlendFactor::SrcAlpha;
        desc.blend.dstColor = BlendFactor::OneMinusSrcAlpha;
        entry.pipeline.reset(entry.fragment
            ? device->createPipeline(desc, vertexShader.get(), entry.fragment.get()) : nullptr);
        return entry.pipeline ? entry.pipeline.get() : fallback;
    }

    struct ShaderEffectEntry {
        std::string body;
        std::unique_ptr<RenderShader> fragment;
        std::unique_ptr<RenderPipeline> pipeline;
    };
    std::map<const void*, ShaderEffectEntry> shaderEffects_;
    struct RgbaEntry {
        std::unique_ptr<RenderTexture> texture;
        std::uint64_t generation = 0;
        int width = 0;
        int height = 0;
    };
    std::map<const void*, RgbaEntry> rgbaTextures_;

    RenderDevice* device;
    std::unique_ptr<RenderShader> vertexShader;
    std::unique_ptr<RenderShader> fragmentShader;
    std::unique_ptr<RenderPipeline> solidPipeline_;
    std::unique_ptr<RenderPipeline> texturedPipeline_;
    std::unique_ptr<RenderPipeline> clearPipeline_;
    std::unique_ptr<RenderBuffer> vertexBuffer_;
    std::vector<std::unique_ptr<RenderBuffer>> retiredBuffers_;
    std::unique_ptr<RenderTexture> fontAtlas_;
};

RenderResources::RenderResources(Render::RenderDevice* device)
    : d(std::make_unique<Impl>(device)) {}
RenderResources::~RenderResources() = default;

Render::RenderDevice* RenderResources::device() const { return d->device; }

class Painter::Impl {
public:
    struct State {
        // Affine item-to-target transform: x' = a*x + c*y + dx,
        // y' = b*x + d*y + dy.
        float a = 1, b = 0, c = 0, d = 1, dx = 0, dy = 0;
        Rect clip;    // target coordinates
        float opacity = 1;

        Point map(float x, float y) const {
            return {a * x + c * y + dx, b * x + d * y + dy};
        }
        bool translationOnly() const {
            return a == 1 && b == 0 && c == 0 && d == 1;
        }
    };

    Impl(RenderResources::Impl& resources, RenderCommandBuffer* commands,
         RenderTarget* target)
        : resources(resources), commands(commands), target(target) {
        State initial;
        const Render::Size size = target->size();
        initial.clip = {0, 0, size.width, size.height};
        stack.push_back(initial);
    }

    State& state() { return stack.back(); }

    /// Maps a local rectangle to a target-space quad. The translation-only
    /// case deliberately keeps the legacy integer rounding bit-exact.
    static void mapRect(const State& s, float x, float y, float w, float h,
                        Point out[4]) {
        if (s.translationOnly()) {
            const float x0 = static_cast<float>(std::lround(x + s.dx));
            const float y0 = static_cast<float>(std::lround(y + s.dy));
            const float x1 = x0 + static_cast<float>(std::lround(w));
            const float y1 = y0 + static_cast<float>(std::lround(h));
            out[0] = {x0, y0};
            out[1] = {x1, y0};
            out[2] = {x1, y1};
            out[3] = {x0, y1};
            return;
        }
        out[0] = s.map(x, y);
        out[1] = s.map(x + w, y);
        out[2] = s.map(x + w, y + h);
        out[3] = s.map(x, y + h);
    }

    /// Records one quad (two triangles) built from the given vertices.
    /// @p scissor overrides the state clip when non-null (used by clear()).
    template <typename V>
    void emitQuad(const V (&verts)[6], RenderPipeline* pipe,
                  RenderTexture* texture, const Rect* scissor = nullptr) {
        RenderBuffer* buffer = resources.vertexBuffer(vertexOffset,
                                                      sizeof(verts));
        if (!buffer || !pipe)
            return;
        buffer->write(verts, sizeof(verts), vertexOffset);
        commands->bindPipeline(pipe);
        commands->setScissor(toRenderRect(scissor ? *scissor : state().clip));
        commands->bindTexture(0, texture);
        commands->bindVertexBuffer(0, buffer, vertexOffset);
        commands->draw(6);
        vertexOffset += sizeof(verts);
    }

    RenderResources::Impl& resources;
    RenderCommandBuffer* commands;
    RenderTarget* target;
    std::uint64_t vertexOffset = 0; // append position within the frame
    std::vector<State> stack;
};

Painter::Painter(RenderResources& resources,
                 Render::RenderCommandBuffer* commands,
                 Render::RenderTarget* target)
    : d(std::make_unique<Impl>(*resources.d, commands, target)) {}
Painter::~Painter() = default;
Painter::Painter(Painter&&) noexcept = default;
Painter& Painter::operator=(Painter&&) noexcept = default;

int Painter::width() const { return d->target->size().width; }
int Painter::height() const { return d->target->size().height; }

void Painter::save() { d->stack.push_back(d->state()); }

void Painter::restore() {
    if (d->stack.size() > 1)
        d->stack.pop_back();
}

void Painter::translate(float dx, float dy) {
    auto& s = d->state();
    s.dx += s.a * dx + s.c * dy;
    s.dy += s.b * dx + s.d * dy;
}

void Painter::rotate(float degrees) {
    constexpr float pi = 3.14159265358979323846f;
    const float angle = degrees * pi / 180.0f;
    const float sn = std::sin(angle);
    const float cs = std::cos(angle);
    auto& s = d->state();
    const float a = s.a, b = s.b, c = s.c, d = s.d;
    s.a = a * cs + c * sn;
    s.c = -a * sn + c * cs;
    s.b = b * cs + d * sn;
    s.d = -b * sn + d * cs;
}

void Painter::scale(float sx, float sy) {
    auto& s = d->state();
    s.a *= sx;
    s.b *= sx;
    s.c *= sy;
    s.d *= sy;
}

void Painter::clipRect(const RectF& rect) {
    const auto& s = d->state();
    const Point p0 = s.map(rect.x, rect.y);
    const Point p1 = s.map(rect.x + rect.width, rect.y);
    const Point p2 = s.map(rect.x + rect.width, rect.y + rect.height);
    const Point p3 = s.map(rect.x, rect.y + rect.height);
    const float minX = std::min({p0.x, p1.x, p2.x, p3.x});
    const float minY = std::min({p0.y, p1.y, p2.y, p3.y});
    const float maxX = std::max({p0.x, p1.x, p2.x, p3.x});
    const float maxY = std::max({p0.y, p1.y, p2.y, p3.y});
    // A scissor is axis-aligned; use the conservative transformed bounds.
    const Rect r{static_cast<int>(std::lround(minX)),
                 static_cast<int>(std::lround(minY)),
                 static_cast<int>(std::lround(maxX - minX)),
                 static_cast<int>(std::lround(maxY - minY))};
    d->state().clip = s.clip.intersected(r);
}

float Painter::opacity() const { return d->state().opacity; }
void Painter::setOpacity(float opacity) {
    d->state().opacity = std::clamp(opacity, 0.0f, 1.0f);
}
void Painter::multiplyOpacity(float factor) {
    d->state().opacity = std::clamp(d->state().opacity * factor, 0.0f, 1.0f);
}

void Painter::fillRect(const RectF& rect, Color color) {
    const auto& s = d->state();
    // Effective alpha, bit-exact with the legacy per-pixel blend formula.
    const unsigned a =
        static_cast<unsigned>(static_cast<unsigned>(color.a) * 255 *
                              s.opacity) /
        255;
    if (a == 0)
        return;
    Point q[4];
    Impl::mapRect(s, rect.x, rect.y, rect.width, rect.height, q);
    const std::uint8_t rgba[4] = {color.r, color.g, color.b,
                                  static_cast<std::uint8_t>(a)};
    const SolidVertex verts[6] = {
        {q[0].x, q[0].y, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {q[1].x, q[1].y, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {q[2].x, q[2].y, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {q[0].x, q[0].y, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {q[2].x, q[2].y, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {q[3].x, q[3].y, {rgba[0], rgba[1], rgba[2], rgba[3]}},
    };
    d->emitQuad(verts, d->resources.pipeline(false, true), nullptr);
}

void Painter::clear(Color color) {
    const Render::Size size = d->target->size();
    const float x1 = static_cast<float>(size.width);
    const float y1 = static_cast<float>(size.height);
    const std::uint8_t rgba[4] = {color.r, color.g, color.b, color.a};
    const SolidVertex verts[6] = {
        {0, 0, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {x1, 0, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {x1, y1, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {0, 0, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {x1, y1, {rgba[0], rgba[1], rgba[2], rgba[3]}},
        {0, y1, {rgba[0], rgba[1], rgba[2], rgba[3]}},
    };
    // Overwrite, ignoring the clip/opacity state.
    const Rect full{0, 0, size.width, size.height};
    d->emitQuad(verts, d->resources.pipeline(false, false), nullptr, &full);
}

void Painter::drawText(float x, float y, const std::string& text, Color color,
                       int scale) {
    if (text.empty() || scale <= 0)
        return;
    Render::RenderTexture* atlas = d->resources.fontAtlas();
    if (!atlas)
        return;
    const auto& s = d->state();
    const unsigned a =
        static_cast<unsigned>(static_cast<unsigned>(color.a) * 255 *
                              s.opacity) /
        255;
    if (a == 0)
        return;
    const std::uint8_t rgba[4] = {color.r, color.g, color.b,
                                  static_cast<std::uint8_t>(a)};
    const float cell = 8.0f * static_cast<float>(scale);
    const float baseX = x;
    const float topY = y;
    float penX = 0;
    float penY = 0;
    forEachCodepoint(text, [&](std::uint32_t cp) {
        if (cp == '\n') {
            penX = 0;
            penY += cell;
            return;
        }
        const float advance = cell;
        if (cp == ' ') {
            penX += advance;
            return;
        }
        std::uint32_t glyph = cp;
        if (glyph > 127)
            glyph = 0x3F; // '?'
        const float u0 = glyph * 8.0f / (128.0f * 8.0f);
        const float u1 = (glyph + 1) * 8.0f / (128.0f * 8.0f);
        Point q[4];
        Impl::mapRect(s, baseX + penX, topY + penY, cell, cell, q);
        const TextVertex verts[6] = {
            {q[0].x, q[0].y, {rgba[0], rgba[1], rgba[2], rgba[3]}, u0, 0},
            {q[1].x, q[1].y, {rgba[0], rgba[1], rgba[2], rgba[3]}, u1, 0},
            {q[2].x, q[2].y, {rgba[0], rgba[1], rgba[2], rgba[3]}, u1, 1},
            {q[0].x, q[0].y, {rgba[0], rgba[1], rgba[2], rgba[3]}, u0, 0},
            {q[2].x, q[2].y, {rgba[0], rgba[1], rgba[2], rgba[3]}, u1, 1},
            {q[3].x, q[3].y, {rgba[0], rgba[1], rgba[2], rgba[3]}, u0, 1},
        };
        d->emitQuad(verts, d->resources.pipeline(true, true), atlas);
        penX += advance;
    });
}

void Painter::drawRgba(const RectF& target, const void* rgba, int imageWidth,
                       int imageHeight, const void* cacheKey,
                       std::uint64_t generation, Color tint) {
    if (!rgba || target.width <= 0 || target.height <= 0)
        return;
    Render::RenderTexture* texture = d->resources.rgbaTexture(
        cacheKey, generation, imageWidth, imageHeight, rgba);
    if (!texture)
        return;
    const auto& state = d->state();
    const unsigned alpha = static_cast<unsigned>(tint.a) * 255 * state.opacity / 255;
    if (!alpha)
        return;
    const std::uint8_t color[4] = {tint.r, tint.g, tint.b,
                                   static_cast<std::uint8_t>(alpha)};
    Point q[4];
    Impl::mapRect(state, target.x, target.y, target.width, target.height, q);
    const TextVertex vertices[6] = {
        {q[0].x, q[0].y, {color[0], color[1], color[2], color[3]}, 0, 0},
        {q[1].x, q[1].y, {color[0], color[1], color[2], color[3]}, 1, 0},
        {q[2].x, q[2].y, {color[0], color[1], color[2], color[3]}, 1, 1},
        {q[0].x, q[0].y, {color[0], color[1], color[2], color[3]}, 0, 0},
        {q[2].x, q[2].y, {color[0], color[1], color[2], color[3]}, 1, 1},
        {q[3].x, q[3].y, {color[0], color[1], color[2], color[3]}, 0, 1},
    };
    d->emitQuad(vertices, d->resources.pipeline(true, true), texture);
}

void Painter::drawShader(const RectF& target, const std::string& fragmentMain,
                         const void* cacheKey, Color tint, const void* rgba,
                         int imageWidth, int imageHeight, std::uint64_t generation) {
    if (target.width <= 0 || target.height <= 0)
        return;
    Render::RenderTexture* texture = rgba
        ? d->resources.rgbaTexture(cacheKey, generation, imageWidth, imageHeight, rgba)
        : nullptr;
    Render::RenderPipeline* pipeline = d->resources.customPipeline(cacheKey, fragmentMain);
    if (!pipeline)
        return;
    const auto& state = d->state();
    const unsigned alpha = static_cast<unsigned>(tint.a) * 255 * state.opacity / 255;
    if (!alpha)
        return;
    const std::uint8_t color[4] = {tint.r, tint.g, tint.b, static_cast<std::uint8_t>(alpha)};
    Point q[4];
    Impl::mapRect(state, target.x, target.y, target.width, target.height, q);
    const TextVertex vertices[6] = {
        {q[0].x, q[0].y, {color[0], color[1], color[2], color[3]}, 0, 0},
        {q[1].x, q[1].y, {color[0], color[1], color[2], color[3]}, 1, 0},
        {q[2].x, q[2].y, {color[0], color[1], color[2], color[3]}, 1, 1},
        {q[0].x, q[0].y, {color[0], color[1], color[2], color[3]}, 0, 0},
        {q[2].x, q[2].y, {color[0], color[1], color[2], color[3]}, 1, 1},
        {q[3].x, q[3].y, {color[0], color[1], color[2], color[3]}, 0, 1},
    };
    d->emitQuad(vertices, pipeline, texture);
}

void Painter::textSize(const std::string& text, int scale, float& width,
                       float& height) {
    if (text.empty() || scale <= 0) {
        width = 0;
        height = 0;
        return;
    }
    const float cell = 8.0f * static_cast<float>(scale);
    float lineWidth = 0;
    float maxWidth = 0;
    int lines = 1;
    forEachCodepoint(text, [&](std::uint32_t cp) {
        if (cp == '\n') {
            maxWidth = std::max(maxWidth, lineWidth);
            lineWidth = 0;
            ++lines;
            return;
        }
        lineWidth += cell;
    });
    maxWidth = std::max(maxWidth, lineWidth);
    width = maxWidth;
    height = lines * cell;
}

} // namespace Gui
} // namespace Firmir
