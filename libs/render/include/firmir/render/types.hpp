#pragma once

// Common render types: the portable intersection of OpenGL, Vulkan and
// DirectX needed to draw primitive geometry (points/lines/triangles).

#include <cstdint>
#include <string>
#include <vector>

namespace Firmir {
namespace Render {

struct Size {
    int width = 0;
    int height = 0;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct Viewport {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
    float minDepth = 0;
    float maxDepth = 1;
};

struct ClearValue {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 1;
    float depth = 1;
    std::uint8_t stencil = 0;
};

/// Primitive topologies supported by OpenGL, Vulkan and DirectX.
enum class PrimitiveTopology {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
};

enum class IndexFormat { UInt16, UInt32 };

/// Intersection of commonly supported vertex attribute formats.
enum class VertexFormat {
    Float,
    Float2,
    Float3,
    Float4,
    UByte4Norm,
};

struct VertexAttribute {
    /// Generic attribute index (Vulkan location / D3D semantic slot mapping).
    std::uint32_t location = 0;
    /// Optional semantic/name used by shading languages that need one.
    std::string name;
    VertexFormat format = VertexFormat::Float3;
    std::uint32_t offset = 0;
};

struct VertexLayout {
    std::uint32_t stride = 0;
    std::vector<VertexAttribute> attributes;
};

enum class ShaderStage { Vertex, Fragment };

/// Shader payloads are backend-specific: Vulkan consumes SPIR-V, DirectX
/// consumes DXIL/DXBC compiled from HLSL, OpenGL consumes GLSL.
enum class ShaderLanguage { Spirv, Hlsl, Glsl };

struct ShaderDesc {
    ShaderStage stage = ShaderStage::Vertex;
    ShaderLanguage language = ShaderLanguage::Spirv;
    std::string entryPoint = "main";
    std::vector<std::uint8_t> code;
};

enum class BufferUsage { Vertex, Index, Uniform };

struct BufferDesc {
    BufferUsage usage = BufferUsage::Vertex;
    std::uint64_t size = 0;
    bool dynamic = false;
};

enum class TextureFormat { Rgba8, Bgra8, Depth24Stencil8 };
enum class Filter { Nearest, Linear };
enum class AddressMode { ClampToEdge, Repeat };

struct SamplerDesc {
    Filter minFilter = Filter::Nearest;
    Filter magFilter = Filter::Nearest;
    AddressMode addressU = AddressMode::ClampToEdge;
    AddressMode addressV = AddressMode::ClampToEdge;
};

struct TextureDesc {
    int width = 1;
    int height = 1;
    TextureFormat format = TextureFormat::Rgba8;
    bool renderTarget = false;
    SamplerDesc sampler;
};

enum class CompareOp {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

enum class BlendFactor {
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
};

enum class BlendOp { Add, Subtract, ReverseSubtract };

struct BlendState {
    bool enabled = false;
    BlendFactor srcColor = BlendFactor::SrcAlpha;
    BlendFactor dstColor = BlendFactor::OneMinusSrcAlpha;
    BlendOp colorOp = BlendOp::Add;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::OneMinusSrcAlpha;
    BlendOp alphaOp = BlendOp::Add;
};

enum class CullMode { None, Back, Front };

struct DepthState {
    bool testEnabled = false;
    bool writeEnabled = false;
    CompareOp compare = CompareOp::Less;
};

struct RasterizerState {
    CullMode cullMode = CullMode::None;
    bool frontFaceCounterClockwise = false;
    bool scissorEnabled = false;
};

struct PipelineDesc {
    VertexLayout vertexLayout;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    RasterizerState rasterizer;
    BlendState blend;
    DepthState depth;
};

struct RenderTargetDesc {
    int width = 1;
    int height = 1;
    TextureFormat colorFormat = TextureFormat::Rgba8;
    bool depth = false;
};

struct SwapchainDesc {
    void* nativeWindow = nullptr;
    int width = 1;
    int height = 1;
    bool vsync = true;
};

struct DeviceDesc {
    bool validation = false;
};

} // namespace Render
} // namespace Firmir
