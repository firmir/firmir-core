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
#include <firmir/gui/eglfsintegration.hpp>
#include <firmir/log.hpp>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <gbm.h>

#include <algorithm>
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
    void setGeometry(const Rect& geometry) override { m_geometry = geometry; }
    void setVisible(bool visible) override { m_visible = visible; }
    void present(const std::uint32_t* pixels, const Size& size) override;

private:
    bool initSurface(gbm_device* gbmDevice);
    bool initGl();
    bool makeCurrent();
    std::uint32_t addFramebuffer(gbm_bo* bo);
    void removeFramebuffer(std::uint32_t fbId);
    bool setCrtc(std::uint32_t fbId);
    bool pageFlip(std::uint32_t fbId);
    void releaseDisplayedBuffer();

    int m_fd = -1;
    EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
    EGLConfig m_eglConfig = nullptr;
    EGLSurface m_eglSurface = EGL_NO_SURFACE;
    EGLContext m_eglContext = EGL_NO_CONTEXT;
    gbm_surface* m_gbmSurface = nullptr;
    GLuint m_program = 0;
    GLuint m_texture = 0;
    GLuint m_vertexBuffer = 0;
    OutputInfo m_output;
    Size m_size;
    /// Size of the frame currently stored in m_texture.
    Size m_textureSize;
    // Buffer being scanned out: released after the next successful flip.
    gbm_bo* m_displayedBo = nullptr;
    std::uint32_t m_displayedFb = 0;
    bool m_modesetDone = false;
    bool m_sizeMismatchLogged = false;
    bool m_presentFailedLogged = false;
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
      m_output(std::move(output)) {
    m_size = {m_output.mode.hdisplay, m_output.mode.vdisplay};
    if (!initSurface(gbmDevice))
        return;
    initGl();
}

KmsWindow::~KmsWindow() {
    if (m_eglContext != EGL_NO_CONTEXT && m_eglSurface != EGL_NO_SURFACE)
        eglMakeCurrent(m_eglDisplay, m_eglSurface, m_eglSurface, m_eglContext);
    if (m_program)
        glDeleteProgram(m_program);
    if (m_texture)
        glDeleteTextures(1, &m_texture);
    if (m_vertexBuffer)
        glDeleteBuffers(1, &m_vertexBuffer);
    if (m_eglDisplay != EGL_NO_DISPLAY && m_eglContext != EGL_NO_CONTEXT)
        eglMakeCurrent(m_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE,
                       EGL_NO_CONTEXT);
    releaseDisplayedBuffer();
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
    if (drmIoctl(m_fd, DRM_IOCTL_MODE_SETCRTC, &crtc) != 0)
        return false;
    m_modesetDone = true;
    return true;
}

bool KmsWindow::pageFlip(std::uint32_t fbId) {
    drm_mode_crtc_page_flip flip{};
    flip.crtc_id = m_output.crtcId;
    flip.fb_id = fbId;
    flip.flags = DRM_MODE_PAGE_FLIP_EVENT;
    if (drmIoctl(m_fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip) != 0)
        return false;
    // Wait for the completion event: it frees the previous buffer and
    // throttles rendering to the refresh rate.
    pollfd pfd{m_fd, POLLIN, 0};
    if (::poll(&pfd, 1, 100) <= 0)
        return false;
    drm_event_vblank event{};
    const ssize_t n = ::read(m_fd, &event, sizeof(event));
    return n >= static_cast<ssize_t>(sizeof(drm_event)) &&
        event.base.type == DRM_EVENT_FLIP_COMPLETE;
}

void KmsWindow::releaseDisplayedBuffer() {
    if (!m_gbmSurface || !m_displayedBo)
        return;
    removeFramebuffer(m_displayedFb);
    gbm_surface_release_buffer(m_gbmSurface, m_displayedBo);
    m_displayedBo = nullptr;
    m_displayedFb = 0;
}

void KmsWindow::present(const std::uint32_t* pixels, const Size& size) {
    if (!m_visible || !m_gbmSurface || m_eglSurface == EGL_NO_SURFACE ||
        !m_program || !m_texture || !m_vertexBuffer)
        return;
    if (size.width <= 0 || size.height <= 0)
        return;
    if (!makeCurrent())
        return;

    if (size.width != m_textureSize.width ||
        size.height != m_textureSize.height) {
        // eglfs has no window manager: a window that is not exactly the mode
        // size is stretched over the whole output instead of being dropped,
        // otherwise a non-fullscreen window would leave the screen black.
        glBindTexture(GL_TEXTURE_2D, m_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.width, size.height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_textureSize = size;
        if (!m_sizeMismatchLogged) {
            cLogInfo(lcKms()) << "window size" << size.width << size.height
                              << "does not match the mode" << m_size.width
                              << m_size.height << "- stretching to fullscreen";
            m_sizeMismatchLogged = true;
        }
    }

    glViewport(0, 0, m_size.width, m_size.height);
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
        return;
    }
    gbm_bo* bo = gbm_surface_lock_front_buffer(m_gbmSurface);
    if (!bo) {
        if (!m_presentFailedLogged) {
            cLogWarning(lcKms()) << "gbm_surface_lock_front_buffer failed";
            m_presentFailedLogged = true;
        }
        return;
    }
    const std::uint32_t fbId = addFramebuffer(bo);
    if (!fbId) {
        gbm_surface_release_buffer(m_gbmSurface, bo);
        return;
    }

    bool presented = false;
    if (!m_modesetDone)
        presented = setCrtc(fbId);
    if (!presented)
        presented = pageFlip(fbId);
    if (presented) {
        releaseDisplayedBuffer();
        m_displayedBo = bo;
        m_displayedFb = fbId;
    } else {
        removeFramebuffer(fbId);
        gbm_surface_release_buffer(m_gbmSurface, bo);
    }
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
    while (!m_stop) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!m_stop)
            refreshScreens();
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

