// eglfs KMS device integration: drives the display directly through DRM/KMS
// with GBM surfaces and an EGL/GLES2 context per window (no compositor, no
// libdrm dependency — raw kernel ioctls only).
//
// Each connected connector with a free CRTC becomes a screen; a window
// presented on it uploads the ARGB32 frame into a GBM back buffer, swaps it
// out of EGL and page-flips the resulting buffer onto the CRTC. Screen
// hotplug is detected by polling the connector status.
//
// Requires DRM master (typically: run on a free VT, or with CAP_SYS_ADMIN).
// Device node: FIRMIR_EGLFS_KMS_DEVICE, falling back to FIRMIR_DRM_DEVICE
// (default /dev/dri/card0).

#include <firmir/gui/detail/drmenumeration.hpp>
#include <firmir/gui/detail/kmslayout.hpp>
#include <firmir/gui/eglfsintegration.hpp>
#include <firmir/log.hpp>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

// Older kernel headers lack enum drm_connector_status.
#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#define DRM_MODE_DISCONNECTED 2
#define DRM_MODE_UNKNOWNCONNECTION 3
#endif

using namespace Firmir::Gui;
using Firmir::Gui::detail::drmIoctl;

namespace {

const Firmir::LoggingCategory& lcKms() {
    static const Firmir::LoggingCategory category("gui.eglfs.kms");
    return category;
}

/// DRM_FORMAT_XRGB8888 (fourcc 'XR24'); defined locally so the integration
/// needs neither drm_fourcc.h nor libdrm.
constexpr std::uint32_t DrmFormatXrgb8888 = 0x34325258u;

/// A connected connector with its chosen mode and usable CRTC.
using OutputInfo = Firmir::Gui::detail::DrmOutput;

class KmsScreen : public PlatformScreen {
public:
    explicit KmsScreen(OutputInfo output) : m_output(std::move(output)) {}
    std::string name() const override { return m_output.name; }
    Size size() const override {
        return {m_output.mode.hdisplay, m_output.mode.vdisplay};
    }
    OutputInfo m_output;
};

class KmsIntegration;

/// Uploads the presented ARGB32 frame into a GBM surface and page-flips it
/// onto the screen's CRTC.
class KmsWindow : public PlatformWindow {
public:
    KmsWindow(int fd, EGLDisplay eglDisplay, EGLConfig eglConfig,
              gbm_device* gbmDevice, OutputInfo output);
    ~KmsWindow() override;

    void setTitle(const std::string&) override {}
    /// Window geometry, called from the GUI thread. The window normally ends up
    /// at the output size (see reportOutputSize()), so this usually carries the
    /// mode size; a size the application sets itself is honoured as well (see
    /// applyRequestedSize()).
    void setGeometry(const Rect& geometry) override {
        m_geometry = geometry;
        m_requestedWidth = geometry.width;
        m_requestedHeight = geometry.height;
    }
    void setVisible(bool visible) override {
        m_visible = visible;
        if (visible)
            reportOutputSize();
    }
    void present(const std::uint32_t* pixels, const Size& size) override;

private:
    /// eglfs has no window manager: a shown window *is* the output, so the size
    /// of the output is reported to the window instead of scaling a smaller
    /// frame up to it. The application lays its content out for that size.
    void reportOutputSize();
    bool initSurface(gbm_device* gbmDevice);
    bool initGl();
    bool makeCurrent();
    void releaseCurrent();
    /// Mode-sets the output to the window's requested size when the connector
    /// has a matching mode. Called from present() (render thread).
    void applyRequestedSize();
    /// Recreates the GBM/EGL surface with @p mode and schedules a mode set.
    bool switchMode(const drm_mode_modeinfo& mode);
    /// The mode set did not take: restores the previous mode and surface.
    void rollbackModeSwitch();
    /// Frees the surface replaced by the last switchMode(), which is off screen
    /// by now. Called from present().
    void releaseRetiredSurface();
    std::uint32_t addFramebuffer(gbm_bo* bo);
    void removeFramebuffer(std::uint32_t fbId);
    bool setCrtc(std::uint32_t fbId);
    /// Queues the page flip of @p fbId and returns at once (see collectFlip).
    bool queuePageFlip(std::uint32_t fbId);
    /// Collects the completion event of the flip queued by the previous
    /// present() and frees the buffer that was on screen before it. Called at
    /// the start of present(), i.e. a whole frame later, so it normally costs
    /// nothing: waiting for a flip inside present() burns the refresh period
    /// that just started and halved the frame rate (33 ms frames on a 60 Hz
    /// panel instead of 17).
    bool collectFlip();
    /// Throws away queued flip events after a mode set, whose CRTC state no
    /// longer matches them.
    void drainFlipEvents();
    /// Frees the buffer of a flip whose completion event never arrived, after a
    /// mode set made it irrelevant.
    void dropStaleQueuedFlip();
    void releaseDisplayedBuffer();

    int m_fd = -1;
    EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
    EGLConfig m_eglConfig = nullptr;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
    EGLContext m_eglContext = EGL_NO_CONTEXT;
    gbm_device* m_gbmDevice = nullptr;
    gbm_surface* m_gbmSurface = nullptr;
    GLuint m_program = 0;
    GLuint m_texture = 0;
    GLuint m_vertexBuffer = 0;
    OutputInfo m_output;
    Size m_size;
    /// Size of the frame currently stored in m_texture.
    Size m_textureSize;
    /// Buffer being scanned out (freed when a later flip completes).
    gbm_bo* m_displayedBo = nullptr;
    std::uint32_t m_displayedFb = 0;
    /// Buffer queued by the last page flip: on screen once its event arrives
    /// (see collectFlip).
    gbm_bo* m_queuedBo = nullptr;
    std::uint32_t m_queuedFb = 0;
    bool m_flipPending = false;
    bool m_modesetDone = false;
    bool m_presentFailedLogged = false;
    /// Size the window asked for through setGeometry() (GUI thread).
    std::atomic<int> m_requestedWidth{0};
    std::atomic<int> m_requestedHeight{0};
    /// Whether the output size was already reported to the window.
    bool m_sizeReported = false;
    /// FIRMIR_GUI_KMS_SCALING=stretch: fill the output instead of drawing the
    /// frame 1:1 in the middle of it.
    bool m_stretch = false;
    bool m_geometryLogged = false;
    /// Surface replaced by switchMode(): still being scanned out until the new
    /// mode set goes through, so it is destroyed on the next present().
    gbm_surface* m_retiredSurface = nullptr;
    gbm_bo* m_retiredBo = nullptr;
    std::uint32_t m_retiredFb = 0;
    drm_mode_modeinfo m_retiredMode{};
    /// Size whose mode set failed: never retried (the frame is drawn 1:1).
    int m_rejectedWidth = 0;
    int m_rejectedHeight = 0;
    bool m_modesetFailedLogged = false;
    Rect m_geometry;
    bool m_visible = false;
};

const char* eglErrorString() {
    switch (eglGetError()) {
    case EGL_SUCCESS:
        return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:
        return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
        return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
        return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
        return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONFIG:
        return "EGL_BAD_CONFIG";
    case EGL_BAD_CONTEXT:
        return "EGL_BAD_CONTEXT";
    case EGL_BAD_DISPLAY:
        return "EGL_BAD_DISPLAY";
    case EGL_BAD_MATCH:
        return "EGL_BAD_MATCH";
    case EGL_BAD_NATIVE_PIXMAP:
        return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
        return "EGL_BAD_NATIVE_WINDOW";
    case EGL_BAD_PARAMETER:
        return "EGL_BAD_PARAMETER";
    case EGL_BAD_SURFACE:
        return "EGL_BAD_SURFACE";
    case EGL_CONTEXT_LOST:
        return "EGL_CONTEXT_LOST";
    default:
        return "EGL_<unknown>";
    }
}

const char* quadVertexShader =
    "attribute vec2 pos;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    "    gl_Position = vec4(pos, 0.0, 1.0);\n"
    // Texture row 0 is the top of the frame, GL's origin is bottom-left.
    "    uv = vec2(pos.x * 0.5 + 0.5, 0.5 - pos.y * 0.5);\n"
    "}\n";

const char* quadFragmentShader =
    "precision mediump float;\n"
    "uniform sampler2D tex;\n"
    "varying vec2 uv;\n"
    "void main() {\n"
    // ARGB32 little-endian: the bytes in memory are B,G,R,A. Uploading them
    // as RGBA and swizzling back avoids requiring EXT_texture_format_BGRA8888.
    "    gl_FragColor = texture2D(tex, uv).bgra;\n"
    "}\n";

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
        cLogWarning(lcKms()) << "shader compilation failed:" << log;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint createPresentProgram() {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, quadVertexShader);
    if (!vertex)
        return 0;
    const GLuint fragment =
        compileShader(GL_FRAGMENT_SHADER, quadFragmentShader);
    if (!fragment) {
        glDeleteShader(vertex);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "pos");
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024] = {};
        glGetProgramInfoLog(program, sizeof(log) - 1, nullptr, log);
        cLogWarning(lcKms()) << "program link failed:" << log;
        glDeleteProgram(program);
        return 0;
    }
    glUseProgram(program);
    glUniform1i(glGetUniformLocation(program, "tex"), 0);
    return program;
}

} // namespace


KmsWindow::KmsWindow(int fd, EGLDisplay eglDisplay, EGLConfig eglConfig,
                     gbm_device* gbmDevice, OutputInfo output)
    : m_fd(fd),
      m_eglDisplay(eglDisplay),
      m_eglConfig(eglConfig),
      m_gbmDevice(gbmDevice),
      m_output(std::move(output)) {
    m_size = {m_output.mode.hdisplay, m_output.mode.vdisplay};
    const char* scaling = std::getenv("FIRMIR_GUI_KMS_SCALING");
    m_stretch = scaling && std::strcmp(scaling, "stretch") == 0;
    if (!initSurface(gbmDevice))
        return;
    initGl();
    // The window is created on the GUI thread, but frames are uploaded from the
    // window's render thread (present()). An EGL context can only be current in
    // one thread at a time, so the context used for the one-time GL setup is
    // released here — otherwise every present() fails with EGL_BAD_ACCESS.
    releaseCurrent();
}

KmsWindow::~KmsWindow() {
    // GL objects can only be deleted with the context current; after this the
    // EGL objects go away.
    const bool hasContext = makeCurrent();
    if (hasContext) {
        if (m_program)
            glDeleteProgram(m_program);
        if (m_texture)
            glDeleteTextures(1, &m_texture);
        if (m_vertexBuffer)
            glDeleteBuffers(1, &m_vertexBuffer);
        m_program = 0;
        m_texture = 0;
        m_vertexBuffer = 0;
    }
    releaseCurrent();
    // A queued flip still owns the buffer it will put on screen: collect it
    // before the buffers and the surface go away.
    collectFlip();
    releaseDisplayedBuffer();
    releaseRetiredSurface();
    if (m_eglSurface != EGL_NO_SURFACE)
        eglDestroySurface(m_eglDisplay, m_eglSurface);
    if (m_eglContext != EGL_NO_CONTEXT)
        eglDestroyContext(m_eglDisplay, m_eglContext);
    if (m_gbmSurface)
        gbm_surface_destroy(m_gbmSurface);
    if (m_modesetDone && m_output.savedCrtc.crtc_id) {
        // Put the pre-eglfs CRTC state back.
        drmIoctl(m_fd, DRM_IOCTL_MODE_SETCRTC, &m_output.savedCrtc);
        m_modesetDone = false;
    }
}

bool KmsWindow::initSurface(gbm_device* gbmDevice) {
    m_gbmSurface =
        gbm_surface_create(gbmDevice, m_size.width, m_size.height,
                           GBM_FORMAT_XRGB8888,
                           GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!m_gbmSurface) {
        cLogWarning(lcKms()) << "gbm_surface_create failed"
                             << m_size.width << m_size.height;
        return false;
    }
    m_eglSurface = eglCreateWindowSurface(
        m_eglDisplay, m_eglConfig,
        reinterpret_cast<EGLNativeWindowType>(m_gbmSurface), nullptr);
    if (m_eglSurface == EGL_NO_SURFACE) {
        cLogWarning(lcKms()) << "eglCreateWindowSurface failed:"
                             << eglErrorString();
        return false;
    }
    const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    m_eglContext = eglCreateContext(m_eglDisplay, m_eglConfig, EGL_NO_CONTEXT,
                                    contextAttribs);
    if (m_eglContext == EGL_NO_CONTEXT) {
        cLogWarning(lcKms()) << "eglCreateContext failed:" << eglErrorString();
        return false;
    }
    return true;
}

bool KmsWindow::initGl() {
    if (!makeCurrent())
        return false;
    m_program = createPresentProgram();
    if (!m_program)
        return false;

    glGenBuffers(1, &m_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    static const GLfloat quad[] = {-1.0f, -1.0f, 1.0f, -1.0f,
                                   -1.0f, 1.0f,  1.0f, 1.0f};
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_size.width, m_size.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_textureSize = m_size;
    return true;
}

bool KmsWindow::makeCurrent() {
    if (m_eglSurface == EGL_NO_SURFACE || m_eglContext == EGL_NO_CONTEXT)
        return false;
    if (eglGetCurrentContext() == m_eglContext &&
        eglGetCurrentSurface(EGL_DRAW) == m_eglSurface)
        return true;
    if (!eglMakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface,
                        m_eglContext)) {
        if (!m_presentFailedLogged) {
            cLogWarning(lcKms()) << "eglMakeCurrent failed:" << eglErrorString();
            m_presentFailedLogged = true;
        }
        return false;
    }
    return true;
}

void KmsWindow::releaseCurrent() {
    if (m_eglDisplay == EGL_NO_DISPLAY)
        return;
    if (eglGetCurrentContext() != m_eglContext)
        return;
    eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);
}


std::uint32_t KmsWindow::addFramebuffer(gbm_bo* bo) {
    drm_mode_fb_cmd2 fb{};
    fb.width = m_size.width;
    fb.height = m_size.height;
    fb.pixel_format = DrmFormatXrgb8888;
    fb.handles[0] = gbm_bo_get_handle(bo).u32;
    fb.pitches[0] = static_cast<std::uint32_t>(gbm_bo_get_stride(bo));
    if (drmIoctl(m_fd, DRM_IOCTL_MODE_ADDFB2, &fb) != 0) {
        cLogWarning(lcKms()) << "ADDFB2 failed:" << std::strerror(errno);
        return 0;
    }
    return fb.fb_id;
}

void KmsWindow::removeFramebuffer(std::uint32_t fbId) {
    if (fbId)
        drmIoctl(m_fd, DRM_IOCTL_MODE_RMFB, &fbId);
}

bool KmsWindow::setCrtc(std::uint32_t fbId) {
    drm_mode_crtc crtc{};
    crtc.crtc_id = m_output.crtcId;
    crtc.fb_id = fbId;
    crtc.set_connectors_ptr =
        reinterpret_cast<std::uint64_t>(&m_output.connectorId);
    crtc.count_connectors = 1;
    crtc.mode = m_output.mode;
    crtc.mode_valid = 1;
    if (drmIoctl(m_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) != 0) {
        if (!m_modesetFailedLogged) {
            cLogWarning(lcKms()) << "SETCRTC" << crtc.mode.hdisplay
                                 << crtc.mode.vdisplay << "failed:"
                                 << std::strerror(errno);
            m_modesetFailedLogged = true;
        }
        return false;
    }
    m_modesetDone = true;
    return true;
}

bool KmsWindow::queuePageFlip(std::uint32_t fbId) {
    drm_mode_crtc_page_flip flip{};
    flip.crtc_id = m_output.crtcId;
    flip.fb_id = fbId;
    flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
    if (drmIoctl(m_fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) != 0) {
        if (!m_presentFailedLogged) {
            cLogWarning(lcKms()) << "PAGE_FLIP failed:" << std::strerror(errno);
            m_presentFailedLogged = true;
        }
        return false;
    }
    m_flipPending = true;
    return true;
}

bool KmsWindow::collectFlip() {
    if (!m_flipPending)
        return true;
    drm_event_vblank event{};
    bool done = false;
    pollfd pfd{m_fd, POLLIN, 0};
    if (::poll(&pfd, 1, 100) > 0) {
        const ssize_t n = ::read(m_fd, &event, sizeof(event));
        done = n >= static_cast<ssize_t>(sizeof(drm_event)) &&
            event.base.type == DRM_EVENT_FLIP_COMPLETE;
    }
    if (!done) {
        // A lost event would make the next flip fail with EBUSY, so it is
        // reported once instead of silently capping the frame rate.
        if (!m_presentFailedLogged) {
            cLogWarning(lcKms()) << "page flip event not received";
            m_presentFailedLogged = true;
        }
        return false;
    }
    m_flipPending = false;
    // The buffer that was on screen until now is off screen: the queued one
    // replaced it.
    releaseDisplayedBuffer();
    m_displayedBo = m_queuedBo;
    m_displayedFb = m_queuedFb;
    m_queuedBo = nullptr;
    m_queuedFb = 0;
    return true;
}

void KmsWindow::drainFlipEvents() {
    const bool wasPending = m_flipPending;
    m_flipPending = false;
    drm_event_vblank event{};
    pollfd pfd{m_fd, POLLIN, 0};
    // Give an in-flight event a moment to arrive; a mode set is rare, so the
    // short wait is cheaper than misreading a stale event later.
    if (wasPending)
        ::poll(&pfd, 1, 20);
    while (::poll(&pfd, 1, 0) > 0) {
        if (::read(m_fd, &event, sizeof(event)) <
            static_cast<ssize_t>(sizeof(drm_event)))
            break;
    }
}

void KmsWindow::dropStaleQueuedFlip() {
    if (!m_queuedBo)
        return;
    // The mode set put another framebuffer on the CRTC, so the queued one can
    // be dropped even though its flip never reported completion.
    removeFramebuffer(m_queuedFb);
    gbm_surface_release_buffer(m_retiredSurface ? m_retiredSurface : m_gbmSurface,
                               m_queuedBo);
    m_queuedBo = nullptr;
    m_queuedFb = 0;
    m_flipPending = false;
}

void KmsWindow::releaseDisplayedBuffer() {
    if (!m_gbmSurface || !m_displayedBo)
        return;
    removeFramebuffer(m_displayedFb);
    gbm_surface_release_buffer(m_gbmSurface, m_displayedBo);
    m_displayedBo = nullptr;
    m_displayedFb = 0;
}

/// eglfs has no window manager, so a shown window *is* the output: the window is
/// told the size of the mode it is displayed on (Window::geometryCallback) and
/// the application lays its content out for it. Scaling a window that asked for
/// another size up to the full screen is what this replaces.
void KmsWindow::reportOutputSize() {
    if (m_sizeReported || !geometryCallback || m_size.width <= 0 ||
        m_size.height <= 0)
        return;
    m_sizeReported = true;
    cLogInfo(lcKms()) << "output size" << m_size.width << m_size.height
                      << "given to the window";
    geometryCallback(Rect{0, 0, m_size.width, m_size.height});
}

/// The window asked for a size that is not the current mode: give it a mode of
/// its own if the connector has one. Without such a mode the output keeps its
/// mode and the frame is placed 1:1 by kmsFrameRect() — the window keeps the
/// size it asked for instead of being stretched over the whole screen.
void KmsWindow::applyRequestedSize() {
    const int width = m_requestedWidth.load();
    const int height = m_requestedHeight.load();
    if (width <= 0 || height <= 0)
        return; // the window has not asked for a size yet
    if (width == m_size.width && height == m_size.height)
        return; // already the size of the output
    if (width == m_rejectedWidth && height == m_rejectedHeight)
        return; // the output refused that mode before
    const drm_mode_modeinfo* mode =
        Firmir::Gui::detail::kmsFindMode(m_output.modes, width, height);
    if (!mode)
        return;
    if (switchMode(*mode))
        m_geometryLogged = false;
}

bool KmsWindow::switchMode(const drm_mode_modeinfo& mode) {
    if (!m_gbmDevice)
        return false;
    // A flip queued for the current surface must complete before that surface
    // is retired: its buffer is still scanned out and its event would otherwise
    // be attributed to the new surface.
    collectFlip();
    gbm_surface* surface = gbm_surface_create(
        m_gbmDevice, mode.hdisplay, mode.vdisplay, GBM_FORMAT_XRGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!surface) {
        cLogWarning(lcKms()) << "gbm_surface_create failed for the window size"
                             << mode.hdisplay << mode.vdisplay;
        return false;
    }
    releaseCurrent();
    const EGLSurface previousEglSurface = m_eglSurface;
    m_eglSurface = eglCreateWindowSurface(
        m_eglDisplay, m_eglConfig,
        reinterpret_cast<EGLNativeWindowType>(surface), nullptr);
    if (m_eglSurface == EGL_NO_SURFACE) {
        cLogWarning(lcKms()) << "eglCreateWindowSurface failed:"
                             << eglErrorString();
        m_eglSurface = previousEglSurface;
        gbm_surface_destroy(surface);
        return false;
    }
    if (previousEglSurface != EGL_NO_SURFACE)
        eglDestroySurface(m_eglDisplay, previousEglSurface);

    // The old buffer is still being scanned out until the SETCRTC below puts the
    // new one on the CRTC, so it is retired rather than destroyed here.
    m_retiredSurface = m_gbmSurface;
    m_retiredBo = m_displayedBo;
    m_retiredFb = m_displayedFb;
    m_retiredMode = m_output.mode;
    m_displayedBo = nullptr;
    m_displayedFb = 0;
    m_gbmSurface = surface;
    // A flip of the old surface may still be queued; its event says nothing
    // about the CRTC after the mode set, so it must not be mistaken for the
    // completion of a later one.
    drainFlipEvents();
    m_output.mode = mode;
    m_size = {mode.hdisplay, mode.vdisplay};
    // The frame may be larger than the texture: let present() reallocate it.
    m_textureSize = {0, 0};
    // The CRTC has to take both the new mode and the new buffer: force SETCRTC.
    m_modesetDone = false;
    cLogInfo(lcKms()) << "window size" << m_size.width << m_size.height
                      << "- switched the output mode to match";
    return true;
}

void KmsWindow::releaseRetiredSurface() {
    if (!m_retiredSurface)
        return;
    removeFramebuffer(m_retiredFb);
    if (m_retiredBo)
        gbm_surface_release_buffer(m_retiredSurface, m_retiredBo);
    gbm_surface_destroy(m_retiredSurface);
    m_retiredSurface = nullptr;
    m_retiredBo = nullptr;
    m_retiredFb = 0;
}

/// SetCRTC refused the requested mode (a CRTC cannot always drive every mode its
/// connector reports). Nothing was destroyed yet, so the previous mode, surface
/// and buffer come back, and that size is not tried again: the window falls back
/// to being drawn 1:1 in the middle of the output.
void KmsWindow::rollbackModeSwitch() {
    if (!m_retiredSurface)
        return;
    m_rejectedWidth = m_size.width;
    m_rejectedHeight = m_size.height;
    const drm_mode_modeinfo rejected = m_output.mode;

    releaseCurrent();
    if (m_eglSurface != EGL_NO_SURFACE)
        eglDestroySurface(m_eglDisplay, m_eglSurface);
    if (m_gbmSurface)
        gbm_surface_destroy(m_gbmSurface);

    m_gbmSurface = m_retiredSurface;
    m_retiredSurface = nullptr;
    m_eglSurface = eglCreateWindowSurface(
        m_eglDisplay, m_eglConfig,
        reinterpret_cast<EGLNativeWindowType>(m_gbmSurface), nullptr);
    if (m_eglSurface == EGL_NO_SURFACE)
        cLogWarning(lcKms()) << "eglCreateWindowSurface failed on rollback:"
                             << eglErrorString();
    m_displayedBo = m_retiredBo;
    m_displayedFb = m_retiredFb;
    m_retiredBo = nullptr;
    m_retiredFb = 0;
    m_output.mode = m_retiredMode;
    m_size = {m_output.mode.hdisplay, m_output.mode.vdisplay};
    m_textureSize = {0, 0};
    // The old mode is still on the CRTC, nothing to re-set.
    m_modesetDone = true;
    m_geometryLogged = false;
    cLogInfo(lcKms()) << "mode" << rejected.hdisplay << rejected.vdisplay
                      << "cannot be set for the window size - staying at"
                      << m_size.width << m_size.height
                      << "and drawing the frame 1:1";
}

void KmsWindow::present(const std::uint32_t* pixels, const Size& size) {
    if (!m_visible || !m_gbmSurface || m_eglSurface == EGL_NO_SURFACE ||
        !m_program || !m_texture || !m_vertexBuffer) {
        // A silent early return here leaves the screen black with no clue why.
        if (!m_presentFailedLogged) {
            m_presentFailedLogged = true;
            cLogWarning(lcKms()) << "present() skipped:" << "visible" << m_visible
                                 << "gbm surface" << (m_gbmSurface != nullptr)
                                 << "egl surface"
                                 << (m_eglSurface != EGL_NO_SURFACE) << "gl"
                                 << (m_program != 0 && m_texture != 0 &&
                                     m_vertexBuffer != 0);
        }
        return;
    }
    if (size.width <= 0 || size.height <= 0)
        return;
    // A surface replaced by the previous switchMode() is off screen now.
    if (m_displayedFb)
        releaseRetiredSurface();
    // The flip of the previous frame has completed by now (a whole frame
    // passed): collecting it frees the buffer that was on screen before it.
    // This is *not* a wait for the flip of this frame — see collectFlip.
    collectFlip();
    // A window may ask for another size at any time; follow it (mode set, or
    // 1:1 placement, see kmslayout.hpp) before touching the surface.
    applyRequestedSize();
    if (!makeCurrent())
        return;

    if (size.width != m_textureSize.width ||
        size.height != m_textureSize.height) {
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.width, size.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_textureSize = size;
    }

    // eglfs has no window manager: the frame is shown 1:1 in the middle of the
    // output, never blown up to fill it (the application lays its content out
    // for the size it asked for). Only a frame larger than the output is scaled,
    // and then only down. FIRMIR_GUI_KMS_SCALING=stretch fills the output.
    const Firmir::Gui::detail::PixelRect frame =
        m_stretch ? Firmir::Gui::detail::PixelRect{0, 0, m_size.width,
                                                   m_size.height}
                  : Firmir::Gui::detail::kmsFrameRect(
                        m_size.width, m_size.height, size.width, size.height);
    if (!m_geometryLogged &&
        (size.width != m_size.width || size.height != m_size.height)) {
        m_geometryLogged = true;
        if (m_stretch) {
            cLogInfo(lcKms()) << "window size" << size.width << size.height
                              << "stretched over the" << m_size.width
                              << m_size.height
                              << "output (FIRMIR_GUI_KMS_SCALING=stretch)";
        } else {
            cLogInfo(lcKms()) << "window size" << size.width << size.height
                              << "drawn 1:1 at" << frame.x << frame.y
                              << "in the" << m_size.width << m_size.height
                              << "output (no mode of that size)";
        }
    }

    glViewport(frame.x, frame.y, frame.width, frame.height);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, size.width, size.height, GL_RGBA,
                    GL_UNSIGNED_BYTE, pixels);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(m_program);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (!eglSwapBuffers(m_eglDisplay, m_eglSurface)) {
        if (!m_presentFailedLogged) {
            cLogWarning(lcKms()) << "eglSwapBuffers failed:" << eglErrorString();
            m_presentFailedLogged = true;
        }
        releaseCurrent();
        return;
    }
    gbm_bo* bo = gbm_surface_lock_front_buffer(m_gbmSurface);
    if (!bo) {
        if (!m_presentFailedLogged) {
            cLogWarning(lcKms()) << "gbm_surface_lock_front_buffer failed";
            m_presentFailedLogged = true;
        }
        releaseCurrent();
        return;
    }
    const std::uint32_t fbId = addFramebuffer(bo);
    if (!fbId) {
        gbm_surface_release_buffer(m_gbmSurface, bo);
        releaseCurrent();
        return;
    }

    bool presented = false;
    if (!m_modesetDone) {
        // SETCRTC programs mode and buffer synchronously and generates no flip
        // event, so everything queued before it is stale.
        presented = setCrtc(fbId);
        if (presented) {
            // A flip whose event never arrived is moot now: the CRTC takes the
            // new framebuffer.
            dropStaleQueuedFlip();
            releaseDisplayedBuffer();
            m_displayedBo = bo;
            m_displayedFb = fbId;
            drainFlipEvents();
        }
    } else {
        // Queued, not waited for: the wait would eat the refresh period that
        // starts now and halve the frame rate (33 ms frames on 60 Hz).
        presented = queuePageFlip(fbId);
        if (presented) {
            m_queuedBo = bo;
            m_queuedFb = fbId;
        }
    }
    if (!presented) {
        removeFramebuffer(fbId);
        gbm_surface_release_buffer(m_gbmSurface, bo);
        // A mode set for the window's size did not take: the previous buffer is
        // still on the CRTC, so put its mode and surface back.
        if (m_retiredSurface && !m_modesetDone)
            rollbackModeSwitch();
    }
    // Nothing is drawn between frames, and the render thread of another window
    // may become current in the meantime, so never keep the context bound.
    releaseCurrent();
}

namespace {

/// KMS device: owns the DRM fd, the GBM device, the shared EGL display and
/// the output list; creates KmsWindows on request.
class KmsIntegration : public EglFsIntegration {
public:
    KmsIntegration();
    ~KmsIntegration() override;

    std::string name() const override { return "kms"; }
    std::vector<PlatformScreen*> screens() override;
    PlatformWindow* createWindow(PlatformScreen* screen) override;
    void destroyWindow(PlatformWindow* window) override;

private:
    bool initEgl();
    void refreshScreens();
    void pollHotplug();

    int m_fd = -1;
    std::string m_node;
    gbm_device* m_gbmDevice = nullptr;
    EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
    EGLConfig m_eglConfig = nullptr;
    std::mutex m_mutex; // guards m_screens and m_windows
    std::vector<std::unique_ptr<KmsScreen>> m_screens;
    std::vector<std::unique_ptr<KmsWindow>> m_windows;
    std::thread m_pollThread;
    bool m_stop = false;
    /// Base hotplug poll interval (FIRMIR_EGLFS_HOTPLUG_INTERVAL_MS, 0 = off).
    int m_hotplugBaseMs = 1000;
    // One-shot diagnostics: the hotplug thread retries every second, so an
    // unavailable device would otherwise flood the log.
    bool m_enumerateErrorLogged = false;
    bool m_noOutputsLogged = false;
};

} // namespace

KmsIntegration::KmsIntegration() {
    const char* device = std::getenv("FIRMIR_EGLFS_KMS_DEVICE");
    if (!device || !*device)
        device = std::getenv("FIRMIR_DRM_DEVICE");
    m_node = device && *device ? device : "/dev/dri/card0";
    m_fd = ::open(m_node.c_str(), O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        cLogWarning(lcKms()) << "cannot open" << (m_node + ":")
                             << std::strerror(errno);
        return;
    }
    // Without DRM master the screens can still be enumerated, but SETCRTC and
    // page flips are refused with EACCES, so report it right away instead of
    // failing on the first frame.
    if (drmIoctl(m_fd, DRM_IOCTL_SET_MASTER, nullptr) != 0) {
        cLogWarning(lcKms()) << "cannot become DRM master on" << m_node << ":"
                             << std::strerror(errno)
                             << "- mode setting and page flips will fail";
    } else {
        cLogDebug(lcKms()) << "became DRM master on" << m_node;
    }
    m_gbmDevice = gbm_create_device(m_fd);
    if (!m_gbmDevice) {
        cLogWarning(lcKms()) << "gbm_create_device failed for" << m_node;
        ::close(m_fd);
        m_fd = -1;
        return;
    }
    if (!initEgl()) {
        gbm_device_destroy(m_gbmDevice);
        m_gbmDevice = nullptr;
        ::close(m_fd);
        m_fd = -1;
        return;
    }
    refreshScreens();
    const char* hotplug = std::getenv("FIRMIR_EGLFS_HOTPLUG_INTERVAL_MS");
    if (hotplug && *hotplug) {
        m_hotplugBaseMs = std::atoi(hotplug);
        if (m_hotplugBaseMs < 0)
            m_hotplugBaseMs = 0;
    }
    m_pollThread = std::thread([this] { pollHotplug(); });
}

KmsIntegration::~KmsIntegration() {
    m_stop = true;
    if (m_pollThread.joinable())
        m_pollThread.join();
    {
        // Windows hold GBM surfaces, so they must go before the device.
        std::lock_guard<std::mutex> lock(m_mutex);
        m_windows.clear();
        m_screens.clear();
    }
    if (m_eglDisplay != EGL_NO_DISPLAY)
        eglTerminate(m_eglDisplay);
    if (m_gbmDevice)
        gbm_device_destroy(m_gbmDevice);
    if (m_fd >= 0)
        ::close(m_fd);
}

bool KmsIntegration::initEgl() {
    auto getPlatformDisplay =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay) {
        m_eglDisplay = getPlatformDisplay(EGL_PLATFORM_GBM_KHR, m_gbmDevice,
                                          nullptr);
    } else {
        m_eglDisplay = eglGetDisplay(
            reinterpret_cast<EGLNativeDisplayType>(m_gbmDevice));
    }
    if (m_eglDisplay == EGL_NO_DISPLAY) {
        cLogWarning(lcKms()) << "eglGetDisplay(GBM) failed:"
                             << eglErrorString();
        return false;
    }
    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(m_eglDisplay, &major, &minor)) {
        cLogWarning(lcKms()) << "eglInitialize failed:" << eglErrorString();
        m_eglDisplay = EGL_NO_DISPLAY;
        return false;
    }
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        cLogWarning(lcKms()) << "eglBindAPI(OpenGL ES) failed:"
                             << eglErrorString();
        return false;
    }
    const EGLint configAttribs[] = {EGL_SURFACE_TYPE,
                                    EGL_WINDOW_BIT,
                                    EGL_RENDERABLE_TYPE,
                                    EGL_OPENGL_ES2_BIT,
                                    EGL_RED_SIZE,
                                    8,
                                    EGL_GREEN_SIZE,
                                    8,
                                    EGL_BLUE_SIZE,
                                    8,
                                    EGL_ALPHA_SIZE,
                                    0,
                                    EGL_NONE};
    EGLint count = 0;
    if (!eglChooseConfig(m_eglDisplay, configAttribs, &m_eglConfig, 1,
                         &count) ||
        count < 1 || !m_eglConfig) {
        cLogWarning(lcKms()) << "no EGL window config for OpenGL ES 2:"
                             << eglErrorString();
        m_eglConfig = nullptr;
        return false;
    }
    cLogInfo(lcKms())
        << "EGL"
        << (std::to_string(major) + "." + std::to_string(minor))
        << eglQueryString(m_eglDisplay, EGL_VENDOR);
    return true;
}


std::vector<PlatformScreen*> KmsIntegration::screens() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<PlatformScreen*> result;
    result.reserve(m_screens.size());
    for (const auto& screen : m_screens)
        result.push_back(screen.get());
    return result;
}

PlatformWindow* KmsIntegration::createWindow(PlatformScreen* screen) {
    auto* kmsScreen = static_cast<KmsScreen*>(screen);
    if (!kmsScreen || !m_gbmDevice || m_eglDisplay == EGL_NO_DISPLAY ||
        !m_eglConfig)
        return nullptr;
    auto window = std::make_unique<KmsWindow>(m_fd, m_eglDisplay, m_eglConfig,
                                              m_gbmDevice, kmsScreen->m_output);
    KmsWindow* result = window.get();
    std::lock_guard<std::mutex> lock(m_mutex);
    m_windows.push_back(std::move(window));
    return result;
}

void KmsIntegration::destroyWindow(PlatformWindow* window) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_windows.erase(
        std::remove_if(m_windows.begin(), m_windows.end(),
                       [&](const auto& w) { return w.get() == window; }),
        m_windows.end());
}

void KmsIntegration::refreshScreens() {
    Firmir::Gui::detail::DrmEnumerationResult enumeration =
        Firmir::Gui::detail::enumerateDrmOutputs(m_fd);
    if (!enumeration.error.empty()) {
        if (!m_enumerateErrorLogged) {
            cLogWarning(lcKms()) << "cannot enumerate KMS outputs on" << m_node
                                 << ":" << enumeration.error;
            m_enumerateErrorLogged = true;
        }
        return;
    }
    m_enumerateErrorLogged = false;
    std::vector<OutputInfo> current = std::move(enumeration.outputs);

    if (current.empty() && !m_noOutputsLogged) {
        std::string connectors;
        for (const auto& report : enumeration.connectors) {
            if (!connectors.empty())
                connectors += ", ";
            connectors += "connector-" + std::to_string(report.connectorId) +
                " " + Firmir::Gui::detail::drmConnectorStatusName(report.status) +
                " (" + std::to_string(report.modeCount) + " modes, " +
                std::to_string(report.encoderCount) + " encoders" +
                (report.note.empty() ? "" : ", " + report.note) + ")";
        }
        if (connectors.empty())
            connectors = "the device reports no connectors";
        cLogWarning(lcKms()) << "no usable KMS output on" << m_node << ":"
                             << connectors
                             << "- pick another node with FIRMIR_EGLFS_KMS_DEVICE";
        m_noOutputsLogged = true;
    }
    if (!current.empty() && m_noOutputsLogged) {
        m_noOutputsLogged = false;
        cLogInfo(lcKms()) << "KMS output available again on" << m_node;
    }

    std::vector<std::pair<PlatformScreen*, bool>> events;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_screens.begin(); it != m_screens.end();) {
            const auto stillThere =
                std::any_of(current.begin(), current.end(),
                            [&](const OutputInfo& info) {
                                return info.connectorId ==
                                    (*it)->m_output.connectorId;
                            });
            if (stillThere) {
                ++it;
                continue;
            }
            events.emplace_back(it->get(), false);
            it = m_screens.erase(it);
        }
        for (OutputInfo& info : current) {
            const auto known =
                std::any_of(m_screens.begin(), m_screens.end(),
                            [&](const auto& screen) {
                                return screen->m_output.connectorId ==
                                    info.connectorId;
                            });
            if (known)
                continue;
            m_screens.push_back(std::make_unique<KmsScreen>(std::move(info)));
            events.emplace_back(m_screens.back().get(), true);
        }
    }
    // Delivered outside the lock: handlers may query screens().
    for (const auto& [screen, added] : events)
        if (screenCallback)
            screenCallback(screen, added);
}

void KmsIntegration::pollHotplug() {
    // Hotplug polling is not free: enumerating probes every connector (an EDID
    // read on a real panel) while holding the DRM lock, so it also stalls the
    // page flips of the render thread. On a tablet panel one enumeration takes
    // ~450 ms — a visible hitch once a second. The interval therefore
    // backs off as soon as an enumeration turns out to be expensive, and
    // FIRMIR_EGLFS_HOTPLUG_INTERVAL_MS=0 disables polling completely (no
    // external display on a fixed panel).
    std::chrono::milliseconds interval(m_hotplugBaseMs > 0 ? m_hotplugBaseMs : 1000);
    while (!m_stop) {
        std::this_thread::sleep_for(interval);
        if (m_stop)
            break;
        if (m_hotplugBaseMs <= 0)
            continue; // disabled: keep the thread idle instead of busy-looping
        const auto started = std::chrono::steady_clock::now();
        refreshScreens();
        const auto cost = std::chrono::steady_clock::now() - started;
        const auto base = std::chrono::milliseconds(m_hotplugBaseMs);
        const auto cap = std::chrono::milliseconds(10000);
        interval = cost > std::chrono::milliseconds(50)
            ? std::min(base * 10, cap)
            : base;
    }
}

extern "C" unsigned firmir_eglfs_integration_abi() {
    return FIRMIR_EGLFS_INTEGRATION_ABI;
}

extern "C" EglFsIntegration* firmir_eglfs_create_integration() {
    // Intentionally leaked: the integration outlives the display backend and
    // the plugin is never unloaded (see the eglfs backend).
    return new KmsIntegration;
}

