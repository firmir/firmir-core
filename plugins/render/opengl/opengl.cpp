// OpenGL ES 2 firmir-render backend. Rendering is performed by the GPU with
// GLSL shaders, vertex buffers, textures and FBOs. EGL uses a tiny pbuffer,
// so no native window is required; CpuRenderTarget reads the finished FBO
// back for the existing display backends (Wayland shm, headless, DRM).
//
// Fixed-function-compatible attribute convention (same as the soft backend):
// location 0 = pixel-space position (Float2/Float3), location 1 = RGBA color
// (Float4/UByte4Norm), location 2 = optional UV (Float2). Dummy shader
// payloads select the built-in GLSL programs; payloads containing "void main"
// are compiled as user GLSL.

#include <firmir/render/backend.hpp>
#include <firmir/log.hpp>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#ifdef FIRMIR_RENDER_HAVE_GBM
#include <gbm.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

using namespace Firmir::Render;

namespace {

const Firmir::LoggingCategory& lcGl() {
    static const Firmir::LoggingCategory category("render.opengl");
    return category;
}

class GlDevice;

class GlBuffer : public RenderBuffer {
public:
    GlBuffer(GlDevice* device, const BufferDesc& desc);
    ~GlBuffer() override;

    BufferUsage usage() const override { return m_desc.usage; }
    std::uint64_t size() const override { return m_desc.size; }
    bool write(const void* data, std::uint64_t bytes, std::uint64_t offset) override;
    void bind();

private:
    GlDevice* m_device;
    BufferDesc m_desc;
    std::vector<std::uint8_t> m_data;
    GLuint m_id = 0;
    bool m_dirty = true;
};

class GlTexture : public RenderTexture {
public:
    GlTexture(GlDevice* device, const TextureDesc& desc);
    /// Imports a dma-buf video frame as an EGLImage-backed external texture
    /// (zero-copy; YUV->RGB happens in the sampler via the EGL colorspace
    /// hints). Returns nullptr on any failure (caller falls back to CPU).
    static GlTexture* importDmaBuf(GlDevice* device,
                                   const DmaBufTextureDesc& desc);
    ~GlTexture() override;

    const TextureDesc& desc() const override { return m_desc; }
    bool write(const void* data, std::uint64_t bytes) override;
    bool isExternal() const override { return m_eglImage != nullptr; }
    void bind(int unit = 0);

private:
    GlDevice* m_device;
    TextureDesc m_desc;
    GLuint m_id = 0;
    GLenum m_target = GL_TEXTURE_2D; // GL_TEXTURE_EXTERNAL_OES for dma-buf
    void* m_eglImage = nullptr;      // EGLImageKHR, owned when set
};

class GlShader : public RenderShader {
public:
    explicit GlShader(const ShaderDesc& desc) : m_desc(desc) {}
    ShaderStage stage() const override { return m_desc.stage; }
    const std::string& entryPoint() const override { return m_desc.entryPoint; }
    const ShaderDesc& desc() const { return m_desc; }

private:
    ShaderDesc m_desc;
};

class GlPipeline : public RenderPipeline {
public:
    GlPipeline(GlDevice* device, const PipelineDesc& desc);
    ~GlPipeline() override;

    const PipelineDesc& desc() const override { return m_desc; }
    void use(const Size& targetSize, bool textured);
    bool valid() const { return m_program != 0; }
    bool link(GlShader* vertex, GlShader* fragment);

private:
    GlDevice* m_device;
    PipelineDesc m_desc;
    GLuint m_program = 0;
    GLint m_targetSizeLocation = -1;
    GLint m_texturedLocation = -1;
    GLint m_textureLocation = -1;
};

class GlTarget : public CpuRenderTarget {
public:
    GlTarget(GlDevice* device, const RenderTargetDesc& desc);
    ~GlTarget() override;

    Size size() const override { return {m_desc.width, m_desc.height}; }
    TextureFormat colorFormat() const override { return m_desc.colorFormat; }
    bool hasDepth() const override { return m_desc.depth; }
    const std::uint32_t* pixels() const override;

    void bind();
    void clear(const ClearValue& value);

private:
    GlDevice* m_device;
    RenderTargetDesc m_desc;
    GLuint m_fbo = 0;
    GLuint m_color = 0;
    mutable std::vector<std::uint8_t> m_rgba;
    mutable std::vector<std::uint32_t> m_argb;
};

class GlDevice : public RenderDevice {
public:
    explicit GlDevice(const DeviceDesc& desc);
    ~GlDevice() override;

    bool valid() const { return m_context != EGL_NO_CONTEXT; }
    void makeCurrent();

    std::string backendName() const override { return "opengl"; }
    RenderBuffer* createBuffer(const BufferDesc& desc) override;
    RenderTexture* createTexture(const TextureDesc& desc) override;
    RenderShader* createShader(const ShaderDesc& desc) override;
    RenderPipeline* createPipeline(const PipelineDesc& desc, RenderShader* vertex,
                                   RenderShader* fragment) override;
    RenderTarget* createRenderTarget(const RenderTargetDesc& desc) override;
    RenderSwapchain* createSwapchain(const SwapchainDesc&) override { return nullptr; }
    RenderCommandBuffer* createCommandBuffer() override;
    void submit(RenderCommandBuffer* commands) override;
    bool present(RenderSwapchain*) override { return false; }
    void waitIdle() override;

    // Zero-copy video frames (dma-buf via EGLImage).
    bool supportsDmaBufTextures() const override { return m_hasDmaBuf; }
    RenderTexture* importDmaBufTexture(const DmaBufTextureDesc& desc) override;

    EGLDisplay display() const { return m_display; }
    bool hasDmaBufModifiers() const { return m_hasDmaBufModifiers; }

private:
    bool initDisplay();
    bool useDisplay(EGLDisplay display, const char* what);

    DeviceDesc m_desc;
    EGLDisplay m_display = EGL_NO_DISPLAY;
    EGLSurface m_surface = EGL_NO_SURFACE;
    EGLContext m_context = EGL_NO_CONTEXT;
    // Owned resources of the display chosen by initDisplay(): the DRM node and,
    // when the gbm platform is used, the gbm device created from it.
    int m_platformFd = -1;
#ifdef FIRMIR_RENDER_HAVE_GBM
    gbm_device* m_gbmDevice = nullptr;
#endif
    // Probed at init: EGL_EXT_image_dma_buf_import (+modifiers) and
    // GL_OES_EGL_image_external.
    bool m_hasDmaBuf = false;
    bool m_hasDmaBufModifiers = false;
};

// ------------------------------ GL objects ---------------------------------

GlBuffer::GlBuffer(GlDevice* device, const BufferDesc& desc)
    : m_device(device), m_desc(desc), m_data(desc.size) {
    m_device->makeCurrent();
    glGenBuffers(1, &m_id);
}

GlBuffer::~GlBuffer() {
    m_device->makeCurrent();
    if (m_id)
        glDeleteBuffers(1, &m_id);
}

bool GlBuffer::write(const void* data, std::uint64_t bytes, std::uint64_t offset) {
    if (offset > m_desc.size || bytes > m_desc.size - offset ||
        (bytes && !data))
        return false;
    if (bytes)
        std::memcpy(m_data.data() + offset, data, static_cast<std::size_t>(bytes));
    m_dirty = true;
    return true;
}

void GlBuffer::bind() {
    m_device->makeCurrent();
    const GLenum target = m_desc.usage == BufferUsage::Index
                              ? GL_ELEMENT_ARRAY_BUFFER
                              : GL_ARRAY_BUFFER;
    glBindBuffer(target, m_id);
    if (m_dirty) {
        glBufferData(target, static_cast<GLsizeiptr>(m_data.size()), m_data.data(),
                     m_desc.dynamic ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        m_dirty = false;
    }
}

GlTexture::GlTexture(GlDevice* device, const TextureDesc& desc)
    : m_device(device), m_desc(desc) {
    m_device->makeCurrent();
    glGenTextures(1, &m_id);
    glBindTexture(GL_TEXTURE_2D, m_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    m_desc.sampler.minFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                    m_desc.sampler.magFilter == Filter::Linear ? GL_LINEAR : GL_NEAREST);
    const GLenum wrap = m_desc.sampler.addressU == AddressMode::Repeat
                            ? GL_REPEAT
                            : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}

GlTexture::~GlTexture() {
    m_device->makeCurrent();
    if (m_id)
        glDeleteTextures(1, &m_id);
    if (m_eglImage) {
        auto destroyImage =
            reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
                eglGetProcAddress("eglDestroyImageKHR"));
        if (destroyImage)
            destroyImage(m_device->display(), m_eglImage);
    }
}

bool GlTexture::write(const void* data, std::uint64_t bytes) {
    if (m_eglImage)
        return false; // external textures are producer-written
    const std::uint64_t expected =
        static_cast<std::uint64_t>(m_desc.width) * m_desc.height * 4;
    if (!data || bytes != expected)
        return false;
    m_device->makeCurrent();
    glBindTexture(GL_TEXTURE_2D, m_id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_desc.width, m_desc.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    return glGetError() == GL_NO_ERROR;
}

void GlTexture::bind(int unit) {
    m_device->makeCurrent();
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(m_target, m_id);
}

namespace {

/// DMA-BUF plane attribute triplets for eglCreateImageKHR.
void pushPlaneAttribs(std::vector<EGLint>& attribs, int plane,
                      const DmaBufPlane& p) {
    const EGLint base[4][3] = {
        {EGL_DMA_BUF_PLANE0_FD_EXT, EGL_DMA_BUF_PLANE0_OFFSET_EXT,
         EGL_DMA_BUF_PLANE0_PITCH_EXT},
        {EGL_DMA_BUF_PLANE1_FD_EXT, EGL_DMA_BUF_PLANE1_OFFSET_EXT,
         EGL_DMA_BUF_PLANE1_PITCH_EXT},
        {EGL_DMA_BUF_PLANE2_FD_EXT, EGL_DMA_BUF_PLANE2_OFFSET_EXT,
         EGL_DMA_BUF_PLANE2_PITCH_EXT},
        {EGL_DMA_BUF_PLANE3_FD_EXT, EGL_DMA_BUF_PLANE3_OFFSET_EXT,
         EGL_DMA_BUF_PLANE3_PITCH_EXT},
    };
    attribs.push_back(base[plane][0]);
    attribs.push_back(p.fd);
    attribs.push_back(base[plane][1]);
    attribs.push_back(static_cast<EGLint>(p.offset));
    attribs.push_back(base[plane][2]);
    attribs.push_back(static_cast<EGLint>(p.stride));
}

} // namespace

GlTexture* GlTexture::importDmaBuf(GlDevice* device,
                                   const DmaBufTextureDesc& desc) {
    if (!device->supportsDmaBufTextures() || desc.planeCount <= 0 ||
        desc.planes[0].fd < 0)
        return nullptr;
    auto createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
        eglGetProcAddress("eglCreateImageKHR"));
    auto imageTarget = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!createImage || !imageTarget)
        return nullptr;

    std::vector<EGLint> attribs;
    attribs.push_back(EGL_WIDTH);
    attribs.push_back(desc.width);
    attribs.push_back(EGL_HEIGHT);
    attribs.push_back(desc.height);
    attribs.push_back(EGL_LINUX_DRM_FOURCC_EXT);
    attribs.push_back(static_cast<EGLint>(desc.fourcc));
    const bool withModifiers =
        device->hasDmaBufModifiers() &&
        desc.modifier != Fourcc::ModifierInvalid;
    for (int i = 0; i < desc.planeCount && i < 4; ++i) {
        pushPlaneAttribs(attribs, i, desc.planes[i]);
        if (withModifiers) { // per-plane modifier (same for all planes)
            static const EGLint lo[4] = {
                EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
                EGL_DMA_BUF_PLANE1_MODIFIER_LO_EXT,
                EGL_DMA_BUF_PLANE2_MODIFIER_LO_EXT,
                EGL_DMA_BUF_PLANE3_MODIFIER_LO_EXT};
            static const EGLint hi[4] = {
                EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
                EGL_DMA_BUF_PLANE1_MODIFIER_HI_EXT,
                EGL_DMA_BUF_PLANE2_MODIFIER_HI_EXT,
                EGL_DMA_BUF_PLANE3_MODIFIER_HI_EXT};
            attribs.push_back(lo[i]);
            attribs.push_back(static_cast<EGLint>(desc.modifier & 0xFFFFFFFFu));
            attribs.push_back(hi[i]);
            attribs.push_back(static_cast<EGLint>(desc.modifier >> 32));
        }
    }
    // GPU-side color conversion hints (Mesa honors both when sampling the
    // external texture).
    if (desc.fourcc != Fourcc::Abgr8) {
        attribs.push_back(EGL_YUV_COLOR_SPACE_HINT_EXT);
        attribs.push_back(desc.ycbcrModel == YcbcrModel::Bt601
                              ? EGL_ITU_REC601_EXT
                              : desc.ycbcrModel == YcbcrModel::Bt2020
                                    ? EGL_ITU_REC2020_EXT
                                    : EGL_ITU_REC709_EXT);
        attribs.push_back(EGL_SAMPLE_RANGE_HINT_EXT);
        attribs.push_back(desc.ycbcrRange == YcbcrRange::Full
                              ? EGL_YUV_FULL_RANGE_EXT
                              : EGL_YUV_NARROW_RANGE_EXT);
    }
    attribs.push_back(EGL_NONE);

    device->makeCurrent();
    EGLImageKHR image = createImage(device->display(), EGL_NO_CONTEXT,
                                    EGL_LINUX_DMA_BUF_EXT, nullptr,
                                    attribs.data());
    if (image == EGL_NO_IMAGE_KHR)
        return nullptr;

    auto* texture = new GlTexture(device, TextureDesc{});
    texture->m_desc.width = desc.width;
    texture->m_desc.height = desc.height;
    texture->m_target = GL_TEXTURE_EXTERNAL_OES;
    texture->m_eglImage = image;
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture->m_id);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S,
                    GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T,
                    GL_CLAMP_TO_EDGE);
    imageTarget(GL_TEXTURE_EXTERNAL_OES, image);
    if (glGetError() != GL_NO_ERROR) {
        delete texture;
        return nullptr;
    }
    return texture;
}

RenderTexture* GlDevice::importDmaBufTexture(const DmaBufTextureDesc& desc) {
    return valid() ? GlTexture::importDmaBuf(this, desc) : nullptr;
}

namespace {

const char* kVertexShader = R"GLSL(
precision highp float;
attribute vec2 a_pos;
attribute vec4 a_color;
attribute vec2 a_uv;
uniform vec2 u_targetSize;
varying vec4 v_color;
varying vec2 v_uv;
void main() {
    vec2 ndc = vec2(a_pos.x * 2.0 / u_targetSize.x - 1.0,
                    1.0 - a_pos.y * 2.0 / u_targetSize.y);
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_color = a_color;
    v_uv = a_uv;
}
)GLSL";

const char* kFragmentShader = R"GLSL(
precision mediump float;
varying vec4 v_color;
varying vec2 v_uv;
uniform sampler2D u_texture;
uniform bool u_textured;
void main() {
    vec4 color = v_color;
    if (u_textured)
        color *= texture2D(u_texture, v_uv);
    gl_FragColor = color;
}
)GLSL";

GLuint compileShader(GLenum type, const std::string& source) {
    const GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[2048];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        cLogWarning(lcGl()) << "shader compilation failed:" << log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

std::string shaderSource(const GlShader* shader, GLenum stage) {
    const auto& code = shader->desc().code;
    const std::string payload(code.begin(), code.end());
    if (payload.find("void main") != std::string::npos)
        return payload;
    return stage == GL_VERTEX_SHADER ? kVertexShader : kFragmentShader;
}

} // namespace

GlPipeline::GlPipeline(GlDevice* device, const PipelineDesc& desc)
    : m_device(device), m_desc(desc) {}

bool GlPipeline::link(GlShader* vertex, GlShader* fragment) {
    m_device->makeCurrent();
    const GLuint vs = compileShader(GL_VERTEX_SHADER, shaderSource(vertex, GL_VERTEX_SHADER));
    const GLuint fs =
        compileShader(GL_FRAGMENT_SHADER, shaderSource(fragment, GL_FRAGMENT_SHADER));
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glBindAttribLocation(m_program, 0, "a_pos");
    glBindAttribLocation(m_program, 1, "a_color");
    glBindAttribLocation(m_program, 2, "a_uv");
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = GL_FALSE;
    glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[2048];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        cLogWarning(lcGl()) << "program link failed:" << log;
        glDeleteProgram(m_program);
        m_program = 0;
        return false;
    }
    m_targetSizeLocation = glGetUniformLocation(m_program, "u_targetSize");
    m_texturedLocation = glGetUniformLocation(m_program, "u_textured");
    m_textureLocation = glGetUniformLocation(m_program, "u_texture");
    return true;
}

GlPipeline::~GlPipeline() {
    m_device->makeCurrent();
    if (m_program)
        glDeleteProgram(m_program);
}

void GlPipeline::use(const Size& targetSize, bool textured) {
    m_device->makeCurrent();
    glUseProgram(m_program);
    glUniform2f(m_targetSizeLocation, static_cast<GLfloat>(targetSize.width),
                static_cast<GLfloat>(targetSize.height));
    glUniform1i(m_texturedLocation, textured ? 1 : 0);
    glUniform1i(m_textureLocation, 0);
    const BlendState& blend = m_desc.blend;
    if (blend.enabled) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        auto factor = [](BlendFactor value) -> GLenum {
            switch (value) {
            case BlendFactor::Zero: return GL_ZERO;
            case BlendFactor::One: return GL_ONE;
            case BlendFactor::SrcAlpha: return GL_SRC_ALPHA;
            case BlendFactor::OneMinusSrcAlpha: return GL_ONE_MINUS_SRC_ALPHA;
            case BlendFactor::DstAlpha: return GL_DST_ALPHA;
            case BlendFactor::OneMinusDstAlpha: return GL_ONE_MINUS_DST_ALPHA;
            }
            return GL_ONE;
        };
        glBlendFuncSeparate(factor(blend.srcColor), factor(blend.dstColor),
                            factor(blend.srcAlpha), factor(blend.dstAlpha));
    } else {
        glDisable(GL_BLEND);
    }
    if (m_desc.rasterizer.scissorEnabled)
        glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
}

GlTarget::GlTarget(GlDevice* device, const RenderTargetDesc& desc)
    : m_device(device), m_desc(desc),
      m_rgba(static_cast<std::size_t>(desc.width) * desc.height * 4),
      m_argb(static_cast<std::size_t>(desc.width) * desc.height) {
    m_device->makeCurrent();
    glGenTextures(1, &m_color);
    glBindTexture(GL_TEXTURE_2D, m_color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, desc.width, desc.height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           m_color, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        cLogWarning(lcGl()) << "incomplete framebuffer" << desc.width << desc.height;
}

GlTarget::~GlTarget() {
    m_device->makeCurrent();
    if (m_fbo)
        glDeleteFramebuffers(1, &m_fbo);
    if (m_color)
        glDeleteTextures(1, &m_color);
}

void GlTarget::bind() {
    m_device->makeCurrent();
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_desc.width, m_desc.height);
}

void GlTarget::clear(const ClearValue& value) {
    bind();
    glDisable(GL_SCISSOR_TEST);
    glClearColor(value.r, value.g, value.b, value.a);
    glClear(GL_COLOR_BUFFER_BIT);
}

const std::uint32_t* GlTarget::pixels() const {
    m_device->makeCurrent();
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, m_desc.width, m_desc.height, GL_RGBA, GL_UNSIGNED_BYTE,
                 m_rgba.data());
    for (int y = 0; y < m_desc.height; ++y) {
        const std::uint8_t* src =
            m_rgba.data() + static_cast<std::size_t>(m_desc.height - 1 - y) *
                                m_desc.width * 4;
        for (int x = 0; x < m_desc.width; ++x) {
            m_argb[static_cast<std::size_t>(y) * m_desc.width + x] =
                (static_cast<std::uint32_t>(src[x * 4 + 3]) << 24) |
                (static_cast<std::uint32_t>(src[x * 4 + 0]) << 16) |
                (static_cast<std::uint32_t>(src[x * 4 + 1]) << 8) |
                src[x * 4 + 2];
        }
    }
    return m_argb.data();
}

// ---------------------------- command execution -----------------------------

struct Command {
    enum Kind { BeginPass, EndPass, Scissor, Pipeline, VertexBuffer, IndexBuffer,
                Texture, Draw, DrawIndexed } kind;
    explicit Command(Kind value) : kind(value) {}
    GlTarget* target = nullptr;
    ClearValue clear;
    Rect rect{};
    GlPipeline* pipeline = nullptr;
    GlBuffer* buffer = nullptr;
    std::uint64_t offset = 0;
    IndexFormat indexFormat = IndexFormat::UInt16;
    GlTexture* texture = nullptr;
    std::uint32_t count = 0;
    std::uint32_t first = 0;
    std::int32_t vertexOffset = 0;
};

struct ExecState {
    GlTarget* target = nullptr;
    GlPipeline* pipeline = nullptr;
    GlBuffer* vertices = nullptr;
    GlBuffer* indices = nullptr;
    GlTexture* texture = nullptr;
    std::uint64_t vertexOffset = 0;
    std::uint64_t indexOffset = 0;
    IndexFormat indexFormat = IndexFormat::UInt16;
    Rect scissor{};
};

class GlCommandBuffer : public RenderCommandBuffer {
public:
    explicit GlCommandBuffer(GlDevice* device) : m_device(device) {}

    void begin() override {
        m_commands.clear();
        m_recording = true;
    }
    void end() override { m_recording = false; }
    void beginRenderPass(RenderTarget* target, const ClearValue& clear) override {
        if (!m_recording)
            return;
        Command cmd(Command::BeginPass);
        cmd.target = static_cast<GlTarget*>(target);
        cmd.clear = clear;
        m_commands.push_back(cmd);
    }
    void endRenderPass() override {
        if (m_recording)
            m_commands.emplace_back(Command::EndPass);
    }
    void setViewport(const Viewport&) override {}
    void setScissor(const Rect& rect) override {
        Command cmd(Command::Scissor);
        cmd.rect = rect;
        m_commands.push_back(cmd);
    }
    void bindPipeline(RenderPipeline* pipeline) override {
        Command cmd(Command::Pipeline);
        cmd.pipeline = static_cast<GlPipeline*>(pipeline);
        m_commands.push_back(cmd);
    }
    void bindVertexBuffer(std::uint32_t slot, RenderBuffer* buffer,
                          std::uint64_t offset) override {
        if (slot != 0)
            return;
        Command cmd(Command::VertexBuffer);
        cmd.buffer = static_cast<GlBuffer*>(buffer);
        cmd.offset = offset;
        m_commands.push_back(cmd);
    }
    void bindIndexBuffer(RenderBuffer* buffer, IndexFormat format,
                         std::uint64_t offset) override {
        Command cmd(Command::IndexBuffer);
        cmd.buffer = static_cast<GlBuffer*>(buffer);
        cmd.indexFormat = format;
        cmd.offset = offset;
        m_commands.push_back(cmd);
    }
    void bindUniformBuffer(std::uint32_t, RenderBuffer*, std::uint64_t,
                           std::uint64_t) override {}
    void bindTexture(std::uint32_t slot, RenderTexture* texture) override {
        if (slot != 0)
            return;
        Command cmd(Command::Texture);
        cmd.texture = static_cast<GlTexture*>(texture);
        m_commands.push_back(cmd);
    }
    void draw(std::uint32_t count, std::uint32_t first) override {
        Command cmd(Command::Draw);
        cmd.count = count;
        cmd.first = first;
        m_commands.push_back(cmd);
    }
    void drawIndexed(std::uint32_t count, std::uint32_t first,
                     std::int32_t vertexOffset) override {
        Command cmd(Command::DrawIndexed);
        cmd.count = count;
        cmd.first = first;
        cmd.vertexOffset = vertexOffset;
        m_commands.push_back(cmd);
    }

    void execute() {
        ExecState state;
        m_device->makeCurrent();
        for (const Command& cmd : m_commands) {
            switch (cmd.kind) {
            case Command::BeginPass:
                state.target = cmd.target;
                state.scissor = {0, 0, cmd.target->size().width,
                                 cmd.target->size().height};
                cmd.target->clear(cmd.clear);
                break;
            case Command::EndPass:
                state.target = nullptr;
                break;
            case Command::Scissor:
                state.scissor = cmd.rect;
                if (state.target)
                    glScissor(cmd.rect.x,
                              state.target->size().height - cmd.rect.y - cmd.rect.height,
                              cmd.rect.width, cmd.rect.height);
                break;
            case Command::Pipeline:
                state.pipeline = cmd.pipeline;
                break;
            case Command::VertexBuffer:
                state.vertices = cmd.buffer;
                state.vertexOffset = cmd.offset;
                break;
            case Command::IndexBuffer:
                state.indices = cmd.buffer;
                state.indexFormat = cmd.indexFormat;
                state.indexOffset = cmd.offset;
                break;
            case Command::Texture:
                state.texture = cmd.texture;
                break;
            case Command::Draw:
            case Command::DrawIndexed:
                draw(state, cmd);
                break;
            }
        }
        glFlush();
    }

private:
    static GLenum topology(PrimitiveTopology value) {
        switch (value) {
        case PrimitiveTopology::PointList: return GL_POINTS;
        case PrimitiveTopology::LineList: return GL_LINES;
        case PrimitiveTopology::LineStrip: return GL_LINE_STRIP;
        case PrimitiveTopology::TriangleList: return GL_TRIANGLES;
        case PrimitiveTopology::TriangleStrip: return GL_TRIANGLE_STRIP;
        }
        return GL_TRIANGLES;
    }

    void draw(const ExecState& state, const Command& cmd) {
        if (!state.target || !state.pipeline || !state.vertices ||
            !state.pipeline->valid())
            return;
        state.target->bind();
        state.pipeline->use(state.target->size(), state.texture != nullptr);
        glScissor(state.scissor.x,
                  state.target->size().height - state.scissor.y - state.scissor.height,
                  state.scissor.width, state.scissor.height);
        state.vertices->bind();
        if (state.texture)
            state.texture->bind();
        for (GLuint location = 0; location <= 2; ++location)
            glDisableVertexAttribArray(location);
        const auto baseVertex = cmd.kind == Command::DrawIndexed ? cmd.vertexOffset : 0;
        for (const VertexAttribute& attr : state.pipeline->desc().vertexLayout.attributes) {
            if (attr.location > 2)
                continue;
            GLint size = 0;
            GLenum type = GL_FLOAT;
            GLboolean normalized = GL_FALSE;
            switch (attr.format) {
            case VertexFormat::Float: size = 1; break;
            case VertexFormat::Float2: size = 2; break;
            case VertexFormat::Float3: size = 3; break;
            case VertexFormat::Float4: size = 4; break;
            case VertexFormat::UByte4Norm:
                size = 4;
                type = GL_UNSIGNED_BYTE;
                normalized = GL_TRUE;
                break;
            }
            const std::uint64_t offset = state.vertexOffset + attr.offset +
                static_cast<std::int64_t>(baseVertex) *
                    state.pipeline->desc().vertexLayout.stride;
            glEnableVertexAttribArray(attr.location);
            glVertexAttribPointer(attr.location, size, type, normalized,
                static_cast<GLsizei>(state.pipeline->desc().vertexLayout.stride),
                reinterpret_cast<const GLvoid*>(static_cast<std::uintptr_t>(offset)));
        }
        if (cmd.kind == Command::Draw) {
            glDrawArrays(topology(state.pipeline->desc().topology), cmd.first, cmd.count);
        } else {
            if (!state.indices)
                return;
            state.indices->bind();
            const GLenum type = state.indexFormat == IndexFormat::UInt16
                                    ? GL_UNSIGNED_SHORT
                                    : GL_UNSIGNED_INT;
            const std::uint64_t offset = state.indexOffset +
                static_cast<std::uint64_t>(cmd.first) *
                    (state.indexFormat == IndexFormat::UInt16 ? 2 : 4);
            glDrawElements(topology(state.pipeline->desc().topology), cmd.count, type,
                reinterpret_cast<const GLvoid*>(static_cast<std::uintptr_t>(offset)));
        }
    }

    GlDevice* m_device;
    std::vector<Command> m_commands;
    bool m_recording = false;
};

// --------------------------------- device -----------------------------------

/// Initializes @p display and adopts it; the first one that works wins.
bool GlDevice::useDisplay(EGLDisplay display, const char* what) {
    if (display == EGL_NO_DISPLAY)
        return false;
    if (eglInitialize(display, nullptr, nullptr) != EGL_TRUE)
        return false;
    m_display = display;
    cLogInfo(lcGl()) << "EGL display:" << what;
    return true;
}

/// Picks an EGL display that works in the current environment.
///
/// A desktop session has X11 or Wayland, and EGL_DEFAULT_DISPLAY dispatches to
/// it. On a bare console / eglfs there is no window system at all: the default
/// display cannot be initialized ("XDG_RUNTIME_DIR is invalid or not set"), so
/// the GPU is driven through a DRM node instead — the gbm platform (same path
/// the eglfs KMS integration uses), the device platform, or Mesa's surfaceless
/// platform. All of them support the pbuffer this backend renders into.
bool GlDevice::initDisplay() {
    const bool windowSystem =
        std::getenv("DISPLAY") != nullptr || std::getenv("WAYLAND_DISPLAY") != nullptr;
    auto getPlatformDisplay =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));

    if (windowSystem &&
        useDisplay(eglGetDisplay(EGL_DEFAULT_DISPLAY), "default"))
        return true;

    if (getPlatformDisplay) {
#ifdef FIRMIR_RENDER_HAVE_GBM
#ifdef EGL_PLATFORM_GBM_KHR
        for (const char* node : {"/dev/dri/card0", "/dev/dri/card1",
                                 "/dev/dri/card2"}) {
            const int fd = ::open(node, O_RDWR | O_CLOEXEC);
            if (fd < 0)
                continue;
            gbm_device* device = gbm_create_device(fd);
            if (!device) {
                ::close(fd);
                continue;
            }
            EGLDisplay display = getPlatformDisplay(
                EGL_PLATFORM_GBM_KHR,
                reinterpret_cast<EGLNativeDisplayType>(device), nullptr);
            if (useDisplay(display, node)) {
                m_gbmDevice = device;
                m_platformFd = fd;
                return true;
            }
            gbm_device_destroy(device);
            ::close(fd);
        }
#endif
#endif
#ifdef EGL_PLATFORM_DEVICE_EXT
        for (const char* node : {"/dev/dri/renderD128", "/dev/dri/renderD129",
                                 "/dev/dri/card0", "/dev/dri/card1"}) {
            const int fd = ::open(node, O_RDWR | O_CLOEXEC);
            if (fd < 0)
                continue;
            EGLDisplay display = getPlatformDisplay(
                EGL_PLATFORM_DEVICE_EXT,
                reinterpret_cast<EGLNativeDisplayType>(
                    static_cast<std::intptr_t>(fd)),
                nullptr);
            if (useDisplay(display, node)) {
                m_platformFd = fd;
                return true;
            }
            ::close(fd);
        }
#endif
#ifdef EGL_PLATFORM_SURFACELESS_MESA
        if (useDisplay(getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                          EGL_DEFAULT_DISPLAY, nullptr),
                       "surfaceless"))
            return true;
#endif
    }

    // Last resort: the default display even without a window system (drivers
    // with an fbdev/own platform may still accept it).
    if (!windowSystem && useDisplay(eglGetDisplay(EGL_DEFAULT_DISPLAY), "default"))
        return true;

    cLogWarning(lcGl())
        << "no usable EGL display (tried default, gbm, device and surfaceless)";
    return false;
}

GlDevice::GlDevice(const DeviceDesc& desc) : m_desc(desc) {
    if (!initDisplay())
        return;
    eglBindAPI(EGL_OPENGL_ES_API);
    const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint count = 0;
    if (eglChooseConfig(m_display, configAttributes, &config, 1, &count) != EGL_TRUE ||
        count < 1) {
        cLogWarning(lcGl()) << "eglChooseConfig failed";
        return;
    }
    const EGLint surfaceAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    m_surface = eglCreatePbufferSurface(m_display, config, surfaceAttributes);
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    m_context = eglCreateContext(m_display, config, EGL_NO_CONTEXT, contextAttributes);
    if (m_context == EGL_NO_CONTEXT || m_surface == EGL_NO_SURFACE) {
        cLogWarning(lcGl()) << "EGL context/surface creation failed";
        return;
    }
    makeCurrent();
    cLogInfo(lcGl()) << "OpenGL ES renderer:" <<
        reinterpret_cast<const char*>(glGetString(GL_RENDERER));

    // Zero-copy capability probe: dma-buf EGLImage import + external sampler.
    const auto hasExtension = [](const char* list, const char* name) {
        return list && std::strstr(list, name) != nullptr;
    };
    const char* eglExts = eglQueryString(m_display, EGL_EXTENSIONS);
    const char* glExts =
        reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    m_hasDmaBufModifiers =
        hasExtension(eglExts, "EGL_EXT_image_dma_buf_import_modifiers");
    m_hasDmaBuf =
        hasExtension(eglExts, "EGL_EXT_image_dma_buf_import") &&
        hasExtension(glExts, "GL_OES_EGL_image_external");
    cLogInfo(lcGl()) << "dma-buf import:"
                     << (m_hasDmaBuf ? "supported" : "unsupported")
                     << (m_hasDmaBufModifiers ? " (+modifiers)" : "");
}

GlDevice::~GlDevice() {
    if (m_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (m_surface != EGL_NO_SURFACE)
            eglDestroySurface(m_display, m_surface);
        if (m_context != EGL_NO_CONTEXT)
            eglDestroyContext(m_display, m_context);
        eglTerminate(m_display);
    }
#ifdef FIRMIR_RENDER_HAVE_GBM
    if (m_gbmDevice)
        gbm_device_destroy(m_gbmDevice);
#endif
    if (m_platformFd >= 0)
        ::close(m_platformFd);
}

void GlDevice::makeCurrent() {
    if (m_context != EGL_NO_CONTEXT && eglGetCurrentContext() != m_context)
        eglMakeCurrent(m_display, m_surface, m_surface, m_context);
}

RenderBuffer* GlDevice::createBuffer(const BufferDesc& desc) {
    return valid() ? new GlBuffer(this, desc) : nullptr;
}
RenderTexture* GlDevice::createTexture(const TextureDesc& desc) {
    if (!valid() || desc.width <= 0 || desc.height <= 0 ||
        desc.format == TextureFormat::Depth24Stencil8)
        return nullptr;
    return new GlTexture(this, desc);
}
RenderShader* GlDevice::createShader(const ShaderDesc& desc) {
    return valid() ? new GlShader(desc) : nullptr;
}
RenderPipeline* GlDevice::createPipeline(const PipelineDesc& desc,
                                         RenderShader* vertex,
                                         RenderShader* fragment) {
    if (!valid() || !vertex || !fragment ||
        vertex->stage() != ShaderStage::Vertex ||
        fragment->stage() != ShaderStage::Fragment || desc.vertexLayout.stride == 0)
        return nullptr;
    auto* pipeline = new GlPipeline(this, desc);
    if (!pipeline->link(static_cast<GlShader*>(vertex), static_cast<GlShader*>(fragment))) {
        delete pipeline;
        return nullptr;
    }
    return pipeline;
}
RenderTarget* GlDevice::createRenderTarget(const RenderTargetDesc& desc) {
    if (!valid() || desc.width <= 0 || desc.height <= 0 ||
        desc.colorFormat == TextureFormat::Depth24Stencil8)
        return nullptr;
    return new GlTarget(this, desc);
}
RenderCommandBuffer* GlDevice::createCommandBuffer() {
    return valid() ? new GlCommandBuffer(this) : nullptr;
}
void GlDevice::submit(RenderCommandBuffer* commands) {
    if (commands)
        static_cast<GlCommandBuffer*>(commands)->execute();
}
void GlDevice::waitIdle() {
    makeCurrent();
    glFinish();
}

class GlBackend : public RenderBackend {
public:
    static GlBackend& instance() {
        static GlBackend backend;
        return backend;
    }
    std::string name() const override { return "opengl"; }
    RenderDevice* createDevice(const DeviceDesc& desc) override {
        auto* device = new GlDevice(desc);
        if (!device->valid()) {
            delete device;
            return nullptr;
        }
        return device;
    }
};

} // namespace

extern "C" Firmir::Render::RenderBackend* firmir_render_create_backend() {
    return &GlBackend::instance();
}
