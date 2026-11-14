// drm_egl client backend: renders the frame into a GPU buffer and hands it to
// the compositor without the wl_shm round trip.
//
// The backend needs three things, and reports exactly which one is missing:
//  * a dma-buf transport to the compositor (zwp_linux_dmabuf_v1) — a GPU
//    buffer cannot be attached to a wl_surface without one;
//  * an EGL implementation that can import and export dma-bufs
//    (EGL_KHR_image_base + EGL_EXT_image_dma_buf_import): the client renders
//    into a GBM buffer and wraps it with eglCreateImageKHR, the compositor
//    imports the other side with eglBindWaylandDisplayWL/eglCreateImageKHR;
//  * a DRM render node to allocate those buffers from (GBM).
//
// The transport plugin (linux_dmabuf_v1) is deliberately not implemented yet,
// so on today's compositors the probe stops at the first check and the loader
// falls back to shm. The probe itself is real: it opens the render node,
// initialises EGL on the Wayland display and inspects the extension strings, so
// bringing the transport up only needs the buffer class in this file.

#include <firmir/wayland/client/backend.hpp>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#ifdef FIRMIR_WAYLAND_CLIENT_HAVE_GBM
#include <fcntl.h>
#include <gbm.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstring>
#include <string>

namespace {

using GetPlatformDisplayProc = EGLDisplay (*)(EGLenum platform, void* native,
                                             const EGLint* attribs);

std::string eglErrorText(const char* what) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%s failed (0x%04x)", what,
                  static_cast<unsigned>(eglGetError()));
    return buffer;
}

/// Opens the first DRM render node, or -1 when the machine has none (a
/// headless container, a VM without a GPU, ...).
int openRenderNode() {
#ifdef FIRMIR_WAYLAND_CLIENT_HAVE_GBM
    for (int index = 128; index <= 135; ++index) {
        char path[32];
        std::snprintf(path, sizeof(path), "/dev/dri/renderD%d", index);
        const int fd = ::open(path, O_RDWR | O_CLOEXEC);
        if (fd >= 0)
            return fd;
    }
#endif
    return -1;
}

struct Probe {
    bool ok = false;
    std::string reason;
};

Probe probe(const Firmir::Wayland::Client::BackendParams& params) {
    if (!params.dmabuf)
        return {false, "the compositor advertises no dma-buf transport "
                       "(zwp_linux_dmabuf_v1)"};

    const int node = openRenderNode();
    if (node < 0)
        return {false, "no DRM render node (/dev/dri/renderD*) is available"};

    auto getPlatformDisplay = reinterpret_cast<GetPlatformDisplayProc>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));
    EGLDisplay display =
        getPlatformDisplay
            ? getPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, params.display, nullptr)
            : eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(params.display));
    ::close(node);
    if (display == EGL_NO_DISPLAY)
        return {false, eglErrorText("eglGetPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR)")};

    EGLint major = 0;
    EGLint minor = 0;
    if (!eglInitialize(display, &major, &minor))
        return {false, eglErrorText("eglInitialize")};

    const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
    const std::string available = extensions ? extensions : "";
    eglTerminate(display);

    for (const char* required :
         {"EGL_KHR_image_base", "EGL_EXT_image_dma_buf_import"}) {
        if (available.find(required) == std::string::npos) {
            std::string reason = "EGL lacks ";
            reason += required;
            return {false, reason};
        }
    }
    return {true, {}};
}

} // namespace

extern "C" Firmir::Wayland::Client::Backend*
firmir_wayland_client_create_backend(
    const Firmir::Wayland::Client::BackendParams& params) {
    const Probe result = probe(params);
    if (!result.ok) {
        std::fprintf(stderr, "firmir-wayland-client: drm_egl unavailable: %s\n",
                     result.reason.c_str());
        return nullptr;
    }
    // The GPU side is ready; only the transport that turns a dma-buf into a
    // wl_buffer is missing (zwp_linux_dmabuf_v1). Until it exists, handing the
    // frame over is impossible, so let the loader pick the next backend.
    std::fprintf(stderr,
                 "firmir-wayland-client: drm_egl is ready, but the dma-buf "
                 "attachment (linux_dmabuf_v1) is not implemented yet\n");
    return nullptr;
}
