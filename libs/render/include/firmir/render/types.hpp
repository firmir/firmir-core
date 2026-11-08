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

// ---------------------------------------------------------------------------
// External (zero-copy) textures: dma-buf video frames imported by the device.
// ---------------------------------------------------------------------------

/// YUV color conversion model for imported video textures (ITU-T matrix).
enum class YcbcrModel { Bt601, Bt709, Bt2020 };
/// YUV sample range: limited (16-235/240) or full (0-255).
enum class YcbcrRange { Limited, Full };

/// DRM/KMS fourcc codes used by imported video frames (linux/drm_fourcc.h
/// values, redeclared to keep this header free of platform includes).
namespace Fourcc {
constexpr std::uint32_t Abgr8 = 0x34324241; // 'AB24' DRM_FORMAT_ABGR8888
                                            // (memory order R,G,B,A)
constexpr std::uint32_t Nv12 = 0x3231564E;  // 'NV12'
constexpr std::uint32_t P010 = 0x30313050;  // 'P010'
constexpr std::uint32_t P016 = 0x36313050;  // 'P016'
constexpr std::uint64_t ModifierInvalid = 0x00FFFFFFFFFFFFFFULL;
} // namespace Fourcc

/// One plane of a dma-buf buffer (multi-planar formats share one fd with
/// different offsets).
struct DmaBufPlane {
    int fd = -1;                  // dma-buf fd, borrowed by the caller
    std::uint32_t offset = 0;     // plane offset within the buffer, bytes
    std::uint32_t stride = 0;     // plane row stride, bytes
};

/**
 * @brief Descriptor for importing a dma-buf video frame as a texture.
 *
 * The fd(s) stay owned by the caller; a successful import keeps them alive
 * internally (dup) for the texture lifetime. YUV formats (NV12/P010/P016)
 * are converted to RGB on the GPU during sampling with the given model and
 * range; RGB formats ignore the ycbcr fields.
 *
 * Synchronization: the caller guarantees the producer (decoder) has finished
 * writing the buffer before the import (e.g. vaSyncSurface); reuse of the
 * buffer by the producer is only allowed after the imported texture is
 * destroyed (see Multimedia::VideoFramePool for the deferred-release model).
 */
struct DmaBufTextureDesc {
    int width = 0;
    int height = 0;
    std::uint32_t fourcc = 0;           // Fourcc::* (DRM format)
    std::uint64_t modifier = Fourcc::ModifierInvalid; // DRM format modifier
    DmaBufPlane planes[4];
    int planeCount = 0;
    YcbcrModel ycbcrModel = YcbcrModel::Bt709;
    YcbcrRange ycbcrRange = YcbcrRange::Limited;
};

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
