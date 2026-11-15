// drm_egl buffer backend: reads client buffers through EGL.
//
// The compositor binds its wl_display to EGL with eglBindWaylandDisplayWL(),
// which lets the EGL implementation advertise the dma-buf path (wl_drm) to
// clients. A client buffer is then described as a dma-buf (plane fd, offset,
// stride, DRM fourcc) and imported as a texture by the compositor's own render
// device: the frame stays on the GPU. Turning the buffer into an EGLImage and
// reading it back through an FBO is only the fallback for a caller that needs
// CPU pixels (the frozen frame of a closed window).
//
// wl_shm buffers cannot be imported by EGL, so this backend reads them the same
// way the shm plugin does — that is what makes it a safe first choice: it is a
// superset of the shm reader rather than an alternative to it.

#include <firmir/wayland/compositor/bufferbackend.hpp>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <fcntl.h>
#include <gbm.h>
#include <unistd.h>
#include <wayland-server.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

using Firmir::Wayland::Compositor::BufferBackend;
using Firmir::Wayland::Compositor::ImportedBuffer;
using Firmir::Wayland::Compositor::WaylandBufferRef;

using GetPlatformDisplayProc = EGLDisplay (*)(EGLenum platform, void* native,
                                             const EGLint* attribs);
using BindWaylandDisplayProc = EGLBoolean (*)(EGLDisplay display,
                                              wl_display* waylandDisplay);
using QueryWaylandBufferProc = EGLBoolean (*)(EGLDisplay display, void* buffer,
                                              EGLint attribute, EGLint* value);
using CreateImageProc = EGLImageKHR (*)(EGLDisplay display, EGLContext context,
                                        EGLenum target, EGLClientBuffer buffer,
                                        const EGLint* attribs);
using DestroyImageProc = EGLBoolean (*)(EGLDisplay display, EGLImageKHR image);
using ImageTargetTextureProc = void (*)(GLenum target, GLeglImageOES image);

/// EGL display bound to the compositor's wl_display, plus the ES2 context the
/// read-back runs in.
class WaylandEgl {
public:
    ~WaylandEgl();

    bool initialize(wl_display* display);
    const std::string& error() const { return mError; }
    GLuint framebuffer() const { return mFramebuffer; }

    bool querySize(wl_resource* buffer, int& width, int& height) const;
    /// eglQueryWaylandBufferWL for a single attribute (size, texture format).
    bool queryAttribute(wl_resource* buffer, EGLint attribute, EGLint& value) const;
    EGLImageKHR createImage(wl_resource* buffer) const;
    void destroyImage(EGLImageKHR image) const;
    void imageToTexture(EGLImageKHR image) const;
    /// Makes the read-back context current on the calling thread: the
    /// compositor paints on its render thread, not on the one that created the
    /// backend.
    bool makeCurrent() const;

private:
    void fail(const std::string& reason);
    bool createDisplay();
    bool createContext();

    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLContext mContext = EGL_NO_CONTEXT;
    EGLSurface mSurface = EGL_NO_SURFACE;
    GLuint mFramebuffer = 0;
    /// DRM device the EGL display was created from (kept alive for it).
    int mNode = -1;
    gbm_device* mGbm = nullptr;
    /// The compositor display bound with eglBindWaylandDisplayWL.
    wl_display* mWaylandDisplay = nullptr;
    QueryWaylandBufferProc mQueryBuffer = nullptr;
    CreateImageProc mCreateImage = nullptr;
    DestroyImageProc mDestroyImage = nullptr;
    ImageTargetTextureProc mImageToTexture = nullptr;
    std::string mError;
};

void WaylandEgl::fail(const std::string& reason) {
    mError = reason;
    char code[32];
    std::snprintf(code, sizeof(code), " (EGL error 0x%04x)",
                  static_cast<unsigned>(eglGetError()));
    mError += code;
}

/// Opens the first DRM render node (-1 when the machine has none).
int openRenderNode() {
    for (int index = 128; index <= 135; ++index) {
        char path[32];
        std::snprintf(path, sizeof(path), "/dev/dri/renderD%d", index);
        const int fd = ::open(path, O_RDWR | O_CLOEXEC);
        if (fd >= 0)
            return fd;
    }
    return -1;
}

bool WaylandEgl::createDisplay() {
    // EGL must NOT be initialised from the compositor's own display: the
    // platform probe talks the client protocol and a server connection cannot
    // answer it (libwayland asserts). Create the EGL display from the GPU and
    // bind the compositor display to it afterwards, the way every compositor
    // does it.
    mNode = openRenderNode();
    if (mNode < 0) {
        mError = "no DRM render node (/dev/dri/renderD*) is available";
        return false;
    }
    mGbm = gbm_create_device(mNode);
    if (!mGbm) {
        mError = "gbm_create_device failed";
        return false;
    }

    auto getPlatformDisplay = reinterpret_cast<GetPlatformDisplayProc>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    mDisplay = getPlatformDisplay
                   ? getPlatformDisplay(EGL_PLATFORM_GBM_KHR, mGbm, nullptr)
                   : eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(mGbm));
    if (mDisplay == EGL_NO_DISPLAY) {
        fail("eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR)");
        return false;
    }

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(mDisplay, &major, &minor)) {
        fail("eglInitialize");
        return false;
    }
    return true;
}

bool WaylandEgl::createContext() {
    // The read-back needs a current context. A pbuffer is preferred; drivers
    // without pbuffer support still work surfaceless (every frame renders into
    // its own FBO anyway).
    const EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_NONE,
    };
    EGLConfig config = nullptr;
    EGLint configCount = 0;
    const bool hasConfig =
        eglChooseConfig(mDisplay, configAttribs, &config, 1, &configCount) &&
        configCount >= 1;
    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    mContext = eglCreateContext(mDisplay, hasConfig ? config : nullptr,
                                EGL_NO_CONTEXT, contextAttribs);
    if (mContext == EGL_NO_CONTEXT) {
        fail("eglCreateContext");
        return false;
    }
    if (hasConfig) {
        const EGLint surfaceAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        mSurface = eglCreatePbufferSurface(mDisplay, config, surfaceAttribs);
    }
    if (!eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) {
        fail("eglMakeCurrent");
        return false;
    }
    glGenFramebuffers(1, &mFramebuffer);
    // A context can be current in one thread only. The backend is created on
    // the thread that starts the compositor, but grab() runs on the render
    // thread, and there eglMakeCurrent() would fail with EGL_BAD_ACCESS while
    // this one is still current. makeCurrent() takes it again per import.
    eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    return true;
}

bool WaylandEgl::initialize(wl_display* waylandDisplay) {
    if (!waylandDisplay) {
        mError = "no wl_display";
        return false;
    }
    if (!createDisplay())
        return false;

    const char* extensions = eglQueryString(mDisplay, EGL_EXTENSIONS);
    const std::string available = extensions ? extensions : "";
    if (available.find("EGL_WL_bind_wayland_display") == std::string::npos) {
        mError = "EGL lacks EGL_WL_bind_wayland_display";
        return false;
    }
    if (available.find("EGL_KHR_image_base") == std::string::npos) {
        mError = "EGL lacks EGL_KHR_image_base";
        return false;
    }

    auto bindWaylandDisplay = reinterpret_cast<BindWaylandDisplayProc>(
        eglGetProcAddress("eglBindWaylandDisplayWL"));
    if (!bindWaylandDisplay) {
        mError = "eglBindWaylandDisplayWL is missing";
        return false;
    }
    mWaylandDisplay = waylandDisplay;
    // From here on the EGL implementation advertises the dma-buf path (wl_drm)
    // to clients and can import their buffers.
    if (!bindWaylandDisplay(mDisplay, waylandDisplay)) {
        fail("eglBindWaylandDisplayWL");
        return false;
    }

    mQueryBuffer = reinterpret_cast<QueryWaylandBufferProc>(
        eglGetProcAddress("eglQueryWaylandBufferWL"));
    mCreateImage = reinterpret_cast<CreateImageProc>(
        eglGetProcAddress("eglCreateImageKHR"));
    mDestroyImage = reinterpret_cast<DestroyImageProc>(
        eglGetProcAddress("eglDestroyImageKHR"));
    mImageToTexture = reinterpret_cast<ImageTargetTextureProc>(
        eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    if (!mQueryBuffer || !mCreateImage || !mDestroyImage || !mImageToTexture) {
        mError = "EGL lacks the wayland-buffer import entry points";
        return false;
    }
    return createContext();
}

WaylandEgl::~WaylandEgl() {
    if (mDisplay == EGL_NO_DISPLAY) {
        if (mGbm)
            gbm_device_destroy(mGbm);
        if (mNode >= 0)
            ::close(mNode);
        return;
    }
    if (mContext != EGL_NO_CONTEXT) {
        eglMakeCurrent(mDisplay, mSurface, mSurface, mContext);
        if (mFramebuffer)
            glDeleteFramebuffers(1, &mFramebuffer);
        eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (mSurface != EGL_NO_SURFACE)
        eglDestroySurface(mDisplay, mSurface);
    if (mContext != EGL_NO_CONTEXT)
        eglDestroyContext(mDisplay, mContext);
    if (mWaylandDisplay) {
        // Drop the wl_drm global before the display goes away.
        auto unbind = reinterpret_cast<EGLBoolean (*)(EGLDisplay, wl_display*)>(
            eglGetProcAddress("eglUnbindWaylandDisplayWL"));
        if (unbind)
            unbind(mDisplay, mWaylandDisplay);
    }
    eglTerminate(mDisplay);
    if (mGbm)
        gbm_device_destroy(mGbm);
    if (mNode >= 0)
        ::close(mNode);
}

bool WaylandEgl::queryAttribute(wl_resource* buffer, EGLint attribute, EGLint& value) const {
    if (!mQueryBuffer || !buffer)
        return false;
    EGLint result = 0;
    if (!mQueryBuffer(mDisplay, buffer, attribute, &result))
        return false;
    value = result;
    return true;
}

bool WaylandEgl::querySize(wl_resource* buffer, int& width, int& height) const {
    EGLint w = 0;
    EGLint h = 0;
    if (!queryAttribute(buffer, EGL_WIDTH, w) || !queryAttribute(buffer, EGL_HEIGHT, h))
        return false;
    if (w <= 0 || h <= 0)
        return false;
    width = w;
    height = h;
    return true;
}

EGLImageKHR WaylandEgl::createImage(wl_resource* buffer) const {
    // A wl_drm buffer has one plane and Mesa expects an empty attribute list;
    // a dma-buf buffer needs EGL_WAYLAND_PLANE_WL, so the plane-0 form is the
    // fallback. eglGetError() is drained before each attempt to keep the
    // error the caller reports attached to the last call.
    const auto create = [this, buffer](const EGLint* attribs) {
        return mCreateImage(mDisplay, EGL_NO_CONTEXT, EGL_WAYLAND_BUFFER_WL,
                            reinterpret_cast<EGLClientBuffer>(buffer), attribs);
    };
    static const EGLint plain[] = {EGL_NONE};
    eglGetError();
    if (EGLImageKHR image = create(plain); image != EGL_NO_IMAGE_KHR)
        return image;
    static const EGLint planeZero[] = {EGL_WAYLAND_PLANE_WL, 0, EGL_NONE};
    return create(planeZero);
}

void WaylandEgl::destroyImage(EGLImageKHR image) const {
    if (image != EGL_NO_IMAGE_KHR)
        mDestroyImage(mDisplay, image);
}

void WaylandEgl::imageToTexture(EGLImageKHR image) const {
    mImageToTexture(GL_TEXTURE_2D, image);
}

bool WaylandEgl::makeCurrent() const {
    if (mDisplay == EGL_NO_DISPLAY || mContext == EGL_NO_CONTEXT)
        return false;
    return eglMakeCurrent(mDisplay, mSurface, mSurface, mContext) == EGL_TRUE;
}

/// One frame read out of a wl_shm buffer (the same read the shm plugin does;
/// kept here so the two plugins stay independent of each other).
class ShmFrame final : public ImportedBuffer {
public:
    explicit ShmFrame(wl_shm_buffer* shm) : mBuffer(shm) {
        if (!shm)
            return;
        mWidth = wl_shm_buffer_get_width(shm);
        mHeight = wl_shm_buffer_get_height(shm);
        const int stride = wl_shm_buffer_get_stride(shm);
        const std::uint32_t format = wl_shm_buffer_get_format(shm);
        if (mWidth <= 0 || mHeight <= 0 || stride < mWidth * 4)
            return;
        if (format != WL_SHM_FORMAT_ARGB8888 && format != WL_SHM_FORMAT_XRGB8888) {
            std::fprintf(stderr,
                         "firmir-wayland-compositor: drm_egl: unsupported shm "
                         "format 0x%08x\n",
                         format);
            return;
        }
        wl_shm_buffer_begin_access(shm);
        mAccess = true;
        const auto* data =
            static_cast<const std::uint8_t*>(wl_shm_buffer_get_data(shm));
        if (!data)
            return;
        mPixels.resize(static_cast<std::size_t>(mWidth) *
                       static_cast<std::size_t>(mHeight));
        for (int y = 0; y < mHeight; ++y)
            std::memcpy(mPixels.data() + static_cast<std::size_t>(y) * mWidth,
                        data + static_cast<std::size_t>(y) * stride,
                        static_cast<std::size_t>(mWidth) * 4);
        // An XRGB client does not have to fill the alpha channel; the painter
        // blends with it, so make the frame opaque like every other backend.
        if (format == WL_SHM_FORMAT_XRGB8888)
            for (std::uint32_t& pixel : mPixels)
                pixel |= 0xff000000u;
        mValid = true;
    }

    ~ShmFrame() override {
        if (mAccess)
            wl_shm_buffer_end_access(mBuffer);
    }

    bool valid() const override { return mValid; }
    int width() const override { return mWidth; }
    int height() const override { return mHeight; }
    std::vector<std::uint32_t>& pixels() override { return mPixels; }

private:
    wl_shm_buffer* mBuffer = nullptr;
    std::vector<std::uint32_t> mPixels;
    int mWidth = 0;
    int mHeight = 0;
    bool mAccess = false;
    bool mValid = false;
};

/// One client buffer as EGL sees it (dma-buf, wl_drm).
///
/// The display path copies nothing: dmaBuf() describes the pixels for
/// RenderDevice::importDmaBufTexture(), which imports the client's frame on the
/// compositor's own GPU context and samples it directly. pixels() is the
/// fallback for a compositor that needs CPU pixels (the frozen frame of a
/// window whose client is gone) and reads them back only when asked to.
class EglFrame final : public ImportedBuffer {
public:
    EglFrame(const WaylandEgl& egl, std::mutex& mutex, wl_resource* buffer)
        : mEgl(egl), mMutex(mutex), mBuffer(buffer) {
        int width = 0;
        int height = 0;
        if (!egl.querySize(buffer, width, height)) {
            std::fprintf(stderr,
                         "firmir-wayland-compositor: drm_egl: eglQueryWaylandBufferWL has no "
                         "size for the client buffer (EGL error 0x%04x)\n",
                         static_cast<unsigned>(eglGetError()));
            return;
        }
        // An external-only (YUV) image cannot be attached to a normal texture;
        // sampling it needs a shader pass this backend does not have.
        EGLint textureFormat = 0;
        if (egl.queryAttribute(buffer, EGL_TEXTURE_FORMAT, textureFormat) &&
            textureFormat == EGL_TEXTURE_EXTERNAL_WL) {
            std::fprintf(stderr,
                         "firmir-wayland-compositor: drm_egl: the %dx%d client buffer is "
                         "external-only, its pixels cannot be read back\n",
                         width, height);
            return;
        }
        mWidth = width;
        mHeight = height;
        mValid = true;
    }

    bool valid() const override { return mValid; }
    int width() const override { return mWidth; }
    int height() const override { return mHeight; }
    /// readBack() reads the frame as GL_RGBA, i.e. already in the byte order the
    /// painter uploads — the grabber must not swap the channels again.
    bool pixelsAreRgba() const override { return true; }
    /// The client's frame as a dma-buf, for the compositor's render device.
    ///
    /// This is the display path: the render device turns the descriptor into an
    /// EGLImage (EGL_LINUX_DMA_BUF_EXT) and then into a texture with
    /// glEGLImageTargetTexture2DOES() in its *own* context, so the frame is
    /// sampled where the scene is painted and never reaches the CPU. It cannot be
    /// done here: this backend's EGL display is not the render device's, and an
    /// EGLImage belongs to the display that created it.
    bool dmaBuf(Firmir::Render::DmaBufTextureDesc& desc) const override {
#ifdef EGL_DMA_BUF_PLANE0_FD_EXT
        if (!mValid || !mBuffer)
            return false;
        EGLint textureFormat = 0;
        // wl_drm carries 32-bit RGB(A) buffers here; the texture format tells
        // which one (an XRGB client leaves the alpha channel to the sampler).
        const bool rgba = mEgl.queryAttribute(mBuffer, EGL_TEXTURE_FORMAT, textureFormat) &&
                          textureFormat == EGL_TEXTURE_RGBA;
        EGLint fd = -1;
        EGLint offset = 0;
        EGLint pitch = 0;
        if (!mEgl.queryAttribute(mBuffer, EGL_DMA_BUF_PLANE0_FD_EXT, fd) || fd < 0)
            return false;
        mEgl.queryAttribute(mBuffer, EGL_DMA_BUF_PLANE0_OFFSET_EXT, offset);
        if (!mEgl.queryAttribute(mBuffer, EGL_DMA_BUF_PLANE0_PITCH_EXT, pitch) || pitch <= 0)
            return false;
        desc.width = mWidth;
        desc.height = mHeight;
        desc.fourcc = rgba ? Firmir::Render::Fourcc::Argb8 : Firmir::Render::Fourcc::Xrgb8;
        desc.modifier = Firmir::Render::Fourcc::ModifierInvalid;
        // The window shows the client's frame at its own size: no filtering is
        // needed, and a linear fetch of a fullscreen external texture is a whole
        // extra pass over the frame on some drivers.
        desc.filter = Firmir::Render::Filter::Nearest;
        desc.planeCount = 1;
        desc.planes[0].fd = fd;
        desc.planes[0].offset = static_cast<std::uint32_t>(offset);
        desc.planes[0].stride = static_cast<std::uint32_t>(pitch);
#ifdef EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT
        EGLint modifierLo = 0;
        EGLint modifierHi = 0;
        if (mEgl.queryAttribute(mBuffer, EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT, modifierLo) &&
            mEgl.queryAttribute(mBuffer, EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT, modifierHi)) {
            desc.modifier =
                (static_cast<std::uint64_t>(static_cast<std::uint32_t>(modifierHi)) << 32) |
                static_cast<std::uint32_t>(modifierLo);
        }
#endif
        return true;
#else
        (void)desc;
        return false;
#endif
    }

    /// CPU pixels of the frame, read back on the first call and cached.
    std::vector<std::uint32_t>& pixels() override {
        if (mPixels.empty() && mValid)
            readBack();
        return mPixels;
    }

private:
    /// EGLImage -> texture -> FBO -> glReadPixels, i.e. the CPU fallback. Only a
    /// compositor that needs the pixels of a frame no longer on screen (the
    /// fade-out of a closed window) asks for them.
    void readBack() {
        // One EGL context, one frame at a time: the read-back runs on the
        // render thread and the context cannot be current in two threads.
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mEgl.makeCurrent()) {
            std::fprintf(stderr,
                         "firmir-wayland-compositor: drm_egl: eglMakeCurrent failed on the "
                         "render thread (EGL error 0x%04x)\n",
                         static_cast<unsigned>(eglGetError()));
            return;
        }
        while (eglGetError() != EGL_SUCCESS) {
        }
        const EGLImageKHR image = mEgl.createImage(mBuffer);
        if (image == EGL_NO_IMAGE_KHR) {
            std::fprintf(stderr,
                         "firmir-wayland-compositor: drm_egl: eglCreateImageKHR failed for a "
                         "%dx%d client buffer (EGL error 0x%04x)\n",
                         mWidth, mHeight, static_cast<unsigned>(eglGetError()));
            return;
        }
        GLuint texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        mEgl.imageToTexture(image);
        const GLenum textureError = glGetError();
        glBindFramebuffer(GL_FRAMEBUFFER, mEgl.framebuffer());
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, texture, 0);
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (textureError != GL_NO_ERROR || status != GL_FRAMEBUFFER_COMPLETE) {
            std::fprintf(stderr,
                         "firmir-wayland-compositor: drm_egl: the %dx%d client buffer cannot be "
                         "read back (texture error 0x%04x, framebuffer status 0x%04x)\n",
                         mWidth, mHeight, static_cast<unsigned>(textureError),
                         static_cast<unsigned>(status));
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glDeleteTextures(1, &texture);
            mEgl.destroyImage(image);
            return;
        }
        // GL hands the bytes out as R,G,B,A — the order the painter uploads — so
        // they are read straight into the destination, one row after another.
        // Row 0 of an imported image is the first row of the client buffer, and
        // glReadPixels starts at y = 0, so the rows arrive top-first already;
        // flipping them here would mirror the window. No per-pixel repack: the
        // frame is 2.4M pixels (a fullscreen tablet window) and a second pass
        // over them cost more than the copy itself (see pixelsAreRgba()).
        mPixels.resize(static_cast<std::size_t>(mWidth) *
                       static_cast<std::size_t>(mHeight));
        while (glGetError() != GL_NO_ERROR) {
        }
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, mWidth, mHeight, GL_RGBA, GL_UNSIGNED_BYTE,
                     mPixels.data());
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDeleteTextures(1, &texture);
        mEgl.destroyImage(image);
        if (glGetError() != GL_NO_ERROR) {
            // A frame that could not be read is no frame: the compositor then
            // leaves the window as it is instead of painting garbage.
            mPixels.clear();
        }
    }

    const WaylandEgl& mEgl;
    std::mutex& mMutex;
    wl_resource* mBuffer = nullptr;
    std::vector<std::uint32_t> mPixels;
    int mWidth = 0;
    int mHeight = 0;
    bool mValid = false;
};

class DrmEglBackend final : public BufferBackend {
public:
    explicit DrmEglBackend(std::unique_ptr<WaylandEgl> egl)
        : mEgl(std::move(egl)) {}

    const char* name() const override { return "drm_egl"; }

    std::unique_ptr<ImportedBuffer> import(
        const WaylandBufferRef& buffer) override {
        if (!buffer.valid())
            return nullptr;
        // EGL cannot import wl_shm, so read it directly: selecting this backend
        // never regresses a CPU-drawn client.
        if (wl_shm_buffer* shm = wl_shm_buffer_get(buffer.handle()))
            return std::make_unique<ShmFrame>(shm);
        // No GL work here: the frame is described as a dma-buf and the
        // compositor's render device imports it. Only a CPU read-back (the
        // frozen frame of a closing window) takes the EGL context, see
        // EglFrame::readBack().
        return std::make_unique<EglFrame>(*mEgl, mImportMutex, buffer.handle());
    }

private:
    std::unique_ptr<WaylandEgl> mEgl;
    std::mutex mImportMutex;
};

} // namespace

extern "C" Firmir::Wayland::Compositor::BufferBackend*
firmir_wayland_compositor_create_backend(wl_display* display) {
    auto egl = std::make_unique<WaylandEgl>();
    if (!egl->initialize(display)) {
        std::fprintf(stderr,
                     "firmir-wayland-compositor: drm_egl unavailable: %s\n",
                     egl->error().c_str());
        return nullptr;
    }
    return new DrmEglBackend(std::move(egl));
}

