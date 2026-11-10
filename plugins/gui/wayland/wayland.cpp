// Wayland display backend for firmir-gui: real windows on any Wayland
// compositor (verified on WSLg). Implements DisplayBackend only; input stays
// with the fallback backend for now.
//
// Threading: Platform drives the backend from the GUI thread, present() runs
// on the render thread, and a dedicated dispatch thread reads compositor
// events (canonical prepare_read/poll loop). All libwayland calls are
// serialized with one mutex (api); wl_buffer release notifications wake
// present() through a condition variable.

#include <firmir/gui/backend.hpp>
#include <firmir/thread.hpp>
#include <firmir/wayland/client/display.hpp>
#include <firmir/wayland/client/registry.hpp>

#include "keytranslate.hpp"

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/mman.h>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <utility>
#include <vector>

using namespace Firmir::Gui;

namespace {

bool verbose() { return std::getenv("FIRMIR_WAYLAND_VERBOSE") != nullptr; }

// Detailed frame-path tracing; enable with FIRMIR_WAYLAND_DEBUG=1. Every line
// carries a CLOCK_MONOTONIC timestamp (seconds.microseconds) so the client
// commit cadence can be compared with the compositor's release / frame-done
// replies. All traced points run under the backend api mutex (present/commit
// on the render thread, release/frame-done/configure on the dispatch thread).
bool debugTrace() {
    static const bool enabled = std::getenv("FIRMIR_WAYLAND_DEBUG") != nullptr;
    return enabled;
}

std::uint64_t monoNs() {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

void trace(const char* fmt, ...) {
    if (!debugTrace())
        return;
    const std::uint64_t now = monoNs();
    std::fprintf(stderr, "firmir-wayland: [t=%llu.%06llu] ",
                 static_cast<unsigned long long>(now / 1000000000ull),
                 static_cast<unsigned long long>((now / 1000ull) % 1000000ull));
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}

class WaylandBackend;

// --------------------------------- screen ---------------------------------

class WaylandScreen : public PlatformScreen {
public:
    WaylandScreen(WaylandBackend* backend, wl_registry* registry,
                  std::uint32_t registryName, std::uint32_t version)
        : m_backend(backend), m_registryName(registryName),
          m_version(std::min(version, 4u)) {
        m_output = static_cast<wl_output*>(wl_registry_bind(
            registry, registryName, &wl_output_interface, m_version));
        wl_output_add_listener(m_output, &listener, this);
    }
    ~WaylandScreen() override {
        if (m_version >= 3)
            wl_output_release(m_output);
        else
            wl_output_destroy(m_output);
    }

    std::string name() const override {
        if (!m_name.empty())
            return m_name;
        return "wayland-" + std::to_string(m_registryName);
    }
    Size size() const override { return m_size; }

    std::uint32_t registryName() const { return m_registryName; }
    bool announced() const { return m_announced; }
    /// First wl_output.done: the initial name/mode burst has been applied and
    /// the screen can be announced (returns true exactly once).
    bool markReadyAndShouldAnnounce() {
        if (m_announced)
            return false;
        m_announced = true;
        return true;
    }

private:
    static void onGeometry(void*, wl_output*, std::int32_t, std::int32_t,
                           std::int32_t, std::int32_t, std::int32_t,
                           const char*, const char*, std::int32_t) {}
    static void onMode(void* data, wl_output*, std::uint32_t flags,
                       std::int32_t w, std::int32_t h, std::int32_t) {
        auto* self = static_cast<WaylandScreen*>(data);
        if (flags & WL_OUTPUT_MODE_CURRENT)
            self->m_size = Size{w, h};
    }
    static void onDone(void* data, wl_output*);
    static void onScale(void*, wl_output*, std::int32_t) {}
    static void onName(void* data, wl_output*, const char* name) {
        static_cast<WaylandScreen*>(data)->m_name = name ? name : "";
    }
    static void onDescription(void*, wl_output*, const char*) {}

    static const wl_output_listener listener;

    WaylandBackend* m_backend;
    wl_output* m_output;
    std::uint32_t m_registryName;
    std::uint32_t m_version;
    std::string m_name;
    Size m_size{0, 0};
    bool m_announced = false;
};

const wl_output_listener WaylandScreen::listener = {
    onGeometry, onMode, onDone, onScale, onName, onDescription,
};

// --------------------------------- window ---------------------------------

class WaylandWindow : public PlatformWindow {
public:
    /// Shared-memory frame buffer (one frame in flight, one spare).
    struct Buffer {
        wl_buffer* handle = nullptr;
        std::uint32_t* data = nullptr;
        int width = 0;
        int height = 0;
        std::size_t bytes = 0;
        int fd = -1;
        bool busy = false; // compositor holds it (release pending)
        WaylandWindow* owner = nullptr;
    };

    explicit WaylandWindow(WaylandBackend* backend);
    ~WaylandWindow() override;

    void setTitle(const std::string& title) override {
        m_title = title;
        xdg_toplevel_set_title(m_toplevel, title.c_str());
    }
    void setGeometry(const Rect& geometry) override {
        // Wayland is compositor-positioned; only the size matters here.
        m_size = Size{geometry.width, geometry.height};
    }
    void setVisible(bool visible) override {
        m_visible = visible;
        if (visible) {
            // Initial empty commit: triggers the first xdg_surface.configure.
            wl_surface_commit(m_surface);
        } else {
            wl_surface_attach(m_surface, nullptr, 0, 0);
            wl_surface_commit(m_surface);
        }
    }

    void present(const std::uint32_t* pixels, const Size& size) override;
    void setMaximized(bool maximized) override {
        if (maximized) xdg_toplevel_set_maximized(m_toplevel);
        else xdg_toplevel_unset_maximized(m_toplevel);
    }
    void setFullscreen(bool fullscreen) override {
        if (fullscreen) xdg_toplevel_set_fullscreen(m_toplevel, nullptr);
        else xdg_toplevel_unset_fullscreen(m_toplevel);
    }
    void setMinimized() override { xdg_toplevel_set_minimized(m_toplevel); }

private:
    void destroyBuffer(Buffer& buffer);
    bool createBuffer(Buffer& buffer, int width, int height);

    static void onBufferRelease(void* data, wl_buffer* handle);
    /// attach + damage + commit of a filled buffer (after the first
    /// xdg_surface.configure only).
    void commitBuffer(Buffer& buffer);

    static void onSurfaceConfigure(void* data, xdg_surface* surface,
                                   std::uint32_t serial);
    static void onToplevelConfigure(void* data, xdg_toplevel*, std::int32_t width,
                                    std::int32_t height, wl_array*) {
        auto* self = static_cast<WaylandWindow*>(data);
        // Remember the size the compositor expects: from the acked
        // xdg_surface.configure on, only buffers of this size may be
        // committed (a mismatched commit is a protocol error on strict
        // compositors — WSLg kills the connection). {0,0} clears the
        // constraint (the compositor leaves the size to the client).
        self->m_pendingSize = Size{width, height};
        if (width > 0 && height > 0) {
            self->m_size = Size{width, height};
            if (self->geometryCallback)
                self->geometryCallback(Rect{0, 0, width, height});
        }
    }
    static void onToplevelClose(void* data, xdg_toplevel*);

    static const wl_buffer_listener bufferListener;
    static const xdg_surface_listener surfaceListener;
    static const xdg_toplevel_listener toplevelListener;

    // Debug-only frame-done probe (FIRMIR_WAYLAND_DEBUG): at most one
    // wl_surface_frame callback in flight, requested just before a commit;
    // its "done" event carries the compositor's presentation timestamp.
    struct FrameProbe {
        WaylandWindow* owner;
        wl_callback* callback;
        std::uint64_t seq;
        std::uint64_t commitNs;
    };
    static void onFrameProbeDone(void* data, wl_callback* callback,
                                 std::uint32_t time);
    static const wl_callback_listener frameProbeListener;

    WaylandBackend* m_backend;
    wl_surface* m_surface = nullptr;
    xdg_surface* m_xdgSurface = nullptr;
    xdg_toplevel* m_toplevel = nullptr;
    std::string m_title;
    Size m_size{0, 0};
    bool m_visible = false;
    Buffer m_buffers[2];
    Buffer* m_pending = nullptr; // filled, waiting for the first configure
    bool m_configured = false;
    /// Size from the last xdg_toplevel.configure, adopted as m_requiredSize
    /// at the following xdg_surface.configure ack.
    Size m_pendingSize{0, 0};
    /// Commits are gated on this size while non-zero (see commitBuffer).
    Size m_requiredSize{0, 0};
    int m_shmCounter = 0;
    bool m_loggedFirstFrame = false;
    FrameProbe* m_frameProbe = nullptr;
    std::uint64_t m_commitSeq = 0;
    std::uint64_t m_lastPresentNs = 0;

    friend class WaylandBackend;
};

// --------------------------------- backend --------------------------------

class WaylandBackend : public DisplayBackend, public InputBackend {
public:
    static WaylandBackend& instance() {
        static WaylandBackend backend;
        return backend;
    }

    // DisplayBackend
    std::vector<PlatformScreen*> screens() override {
        std::lock_guard<std::mutex> lock(api);
        std::vector<PlatformScreen*> result;
        for (const auto& screen : m_screens)
            result.push_back(screen.get());
        return result;
    }

    PlatformWindow* createWindow(PlatformScreen*) override {
        std::lock_guard<std::mutex> lock(api);
        if (!m_compositor || !m_wmBase)
            return nullptr;
        m_windows.push_back(std::make_unique<WaylandWindow>(this));
        return m_windows.back().get();
    }

    void destroyWindow(PlatformWindow* window) override {
        std::lock_guard<std::mutex> lock(api);
        for (auto it = m_windows.begin(); it != m_windows.end(); ++it) {
            if (it->get() == window) {
                m_windows.erase(it);
                return;
            }
        }
    }

    /// wl_output.done handler (dispatch thread, api held): announce the
    /// screen once its initial burst is complete.
    void announceScreen(WaylandScreen* screen) {
        if (screen->markReadyAndShouldAnnounce() && screenCallback)
            screenCallback(screen, true);
    }

    std::mutex api;                         // serializes all libwayland calls
    std::condition_variable bufferReleased; // wl_buffer.release notifications
    wl_compositor* m_compositor = nullptr;
    wl_shm* m_shm = nullptr;
    xdg_wm_base* m_wmBase = nullptr;

    // InputBackend (pointer events from wl_seat/wl_pointer).
    void emitMouse(MouseEvent::Action action, Point pos, int button) {
        if (!eventCallback)
            return;
        InputEvent event;
        event.type = InputEvent::Type::Mouse;
        event.mouse.action = action;
        event.mouse.pos = pos;
        event.mouse.button = button;
        eventCallback(event);
    }

    void emitWheel(Point pos, float deltaX, float deltaY) {
        if (!eventCallback)
            return;
        InputEvent event;
        event.type = InputEvent::Type::Mouse;
        event.mouse.action = MouseEvent::Action::Wheel;
        event.mouse.pos = pos;
        event.mouse.delta = {deltaX, deltaY};
        eventCallback(event);
    }

    void emitKey(bool pressed, std::uint32_t wlKey) {
        if (!eventCallback)
            return;
        InputEvent event;
        event.type = InputEvent::Type::Key;
        event.key.pressed = pressed;
        // wl_keyboard delivers evdev keycodes on the wire (the +8 offset to
        // XKB keycodes is a client-side convention), and the rest of Firmir
        // speaks evdev too — see WaylandInput::translateKey.
        const Firmir::WaylandInput::KeyTranslation key =
            Firmir::WaylandInput::translateKey(m_xkbState, wlKey, pressed);
        event.key.key = key.key;
        event.key.text = key.text;
        eventCallback(event);
    }

    // ---- keyboard (xkbcommon state fed from wl_keyboard events) ----

    void setupKeymap(std::uint32_t format, std::int32_t fd, std::uint32_t size) {
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
            std::fprintf(stderr, "firmir-wayland: unsupported keymap format %u\n",
                         format);
            return;
        }
        char* map = static_cast<char*>(
            ::mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
        if (map == MAP_FAILED)
            return;
        if (!m_xkbContext)
            m_xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        xkb_keymap* newKeymap = nullptr;
        if (m_xkbContext)
            newKeymap = xkb_keymap_new_from_string(
                m_xkbContext, map, XKB_KEYMAP_FORMAT_TEXT_V1,
                XKB_KEYMAP_COMPILE_NO_FLAGS);
        ::munmap(map, size);
        if (!newKeymap)
            return;
        xkb_state* newState = xkb_state_new(newKeymap);
        if (!newState) {
            xkb_keymap_unref(newKeymap);
            return;
        }
        if (m_xkbState)
            xkb_state_unref(m_xkbState);
        if (m_xkbKeymap)
            xkb_keymap_unref(m_xkbKeymap);
        m_xkbKeymap = newKeymap;
        m_xkbState = newState;
        if (verbose())
            std::fprintf(stderr, "firmir-wayland: xkb keymap installed\n");
    }

    static void keymapCb(void* data, wl_keyboard*, std::uint32_t format,
                         std::int32_t fd, std::uint32_t size) {
        static_cast<WaylandBackend*>(data)->setupKeymap(format, fd, size);
        ::close(fd);
    }
    static void keyboardEnterCb(void*, wl_keyboard*, std::uint32_t, wl_surface*,
                                wl_array*) {}
    static void keyboardLeaveCb(void*, wl_keyboard*, std::uint32_t,
                                wl_surface*) {}
    static void keyCb(void* data, wl_keyboard*, std::uint32_t, std::uint32_t,
                      std::uint32_t wlKey, std::uint32_t state) {
        static_cast<WaylandBackend*>(data)->emitKey(
            state == WL_KEYBOARD_KEY_STATE_PRESSED, wlKey);
    }
    static void modifiersCb(void* data, wl_keyboard*, std::uint32_t,
                            std::uint32_t depressed, std::uint32_t latched,
                            std::uint32_t locked, std::uint32_t group) {
        WaylandBackend* self = static_cast<WaylandBackend*>(data);
        if (self->m_xkbState)
            xkb_state_update_mask(self->m_xkbState, depressed, latched, locked,
                                  0, 0, group);
    }
    static void repeatInfoCb(void*, wl_keyboard*, std::int32_t, std::int32_t) {}

    inline static const wl_keyboard_listener keyboardListener = {
        keymapCb, keyboardEnterCb, keyboardLeaveCb, keyCb, modifiersCb,
        repeatInfoCb,
    };

private:
    WaylandBackend();
    ~WaylandBackend() override;

    static void onGlobal(void* data, wl_registry* registry, std::uint32_t name,
                         const char* interface, std::uint32_t version);
    static void onGlobalRemove(void* data, wl_registry*, std::uint32_t name);
    static void onPing(void*, xdg_wm_base* wmBase, std::uint32_t serial) {
        xdg_wm_base_pong(wmBase, serial);
    }
    void threadMain();

    // wl_seat / wl_pointer (dispatch thread, api held).
    void bindSeat(wl_registry* registry, std::uint32_t name,
                  std::uint32_t version);
    void onSeatCapabilities(std::uint32_t capabilities);
    WaylandWindow* windowFor(wl_surface* surface);
    static void onSeatCapabilities(void* data, wl_seat*, std::uint32_t caps);
    static void onSeatName(void*, wl_seat*, const char*) {}
    static void onPointerEnter(void* data, wl_pointer*, std::uint32_t,
                               wl_surface* surface, wl_fixed_t sx,
                               wl_fixed_t sy);
    static void onPointerLeave(void* data, wl_pointer*, std::uint32_t,
                               wl_surface*);
    static void onPointerMotion(void* data, wl_pointer*, std::uint32_t,
                                wl_fixed_t sx, wl_fixed_t sy);
    static void onPointerButton(void* data, wl_pointer*, std::uint32_t,
                                std::uint32_t, std::uint32_t button,
                                std::uint32_t state);
    static void onPointerAxis(void* data, wl_pointer*, std::uint32_t,
                              std::uint32_t axis, wl_fixed_t value);

    static const xdg_wm_base_listener wmBaseListener;
    static const wl_seat_listener seatListener;
    static const wl_pointer_listener pointerListener;

    std::unique_ptr<Firmir::Wayland::Client::Display> m_clientDisplay;
    std::unique_ptr<Firmir::Wayland::Client::Registry> m_clientRegistry;
    wl_display* m_display = nullptr;
    wl_registry* m_registry = nullptr;
    std::vector<std::unique_ptr<WaylandScreen>> m_screens;
    std::vector<std::unique_ptr<WaylandWindow>> m_windows;
    std::thread m_thread;
    std::atomic<bool> m_running{false};

    wl_seat* m_seat = nullptr;
    wl_pointer* m_pointer = nullptr;
    wl_keyboard* m_keyboard = nullptr;
    xkb_context* m_xkbContext = nullptr;
    xkb_keymap* m_xkbKeymap = nullptr;
    xkb_state* m_xkbState = nullptr;
    Point m_pointerPos{0, 0}; // last pointer position (window coordinates)

    friend class WaylandWindow;
    friend class WaylandScreen;
};

const xdg_wm_base_listener WaylandBackend::wmBaseListener = {onPing};

WaylandBackend::WaylandBackend() {
    m_clientDisplay = std::make_unique<Firmir::Wayland::Client::Display>();
    m_display = m_clientDisplay->handle();
    if (!m_display) {
        std::fprintf(stderr, "firmir-wayland: wl_display_connect failed "
                             "(WAYLAND_DISPLAY not set?)\n");
        return;
    }
    m_clientRegistry = m_clientDisplay->createRegistry();
    m_registry = m_clientRegistry ? m_clientRegistry->handle() : nullptr;
    if (!m_registry)
        return;
    m_clientRegistry->setGlobalHandler([this](std::uint32_t name, const char* interface,
                                              std::uint32_t version) {
        onGlobal(this, m_registry, name, interface, version);
    });
    m_clientRegistry->setGlobalRemoveHandler([this](std::uint32_t name) {
        onGlobalRemove(this, m_registry, name);
    });
    // Two roundtrips: globals arrive in the first, the wl_output
    // name/mode/done burst in the second.
    m_clientDisplay->roundtrip();
    m_clientDisplay->roundtrip();
    if (!m_compositor || !m_shm || !m_wmBase)
        std::fprintf(stderr, "firmir-wayland: compositor misses "
                             "wl_compositor/wl_shm/xdg_wm_base\n");
    m_running = true;
    m_thread = std::thread([this] { threadMain(); });
    if (verbose())
        std::fprintf(stderr, "firmir-wayland: connected, %zu screen(s)\n",
                     m_screens.size());
}

WaylandBackend::~WaylandBackend() {
    m_running = false;
    if (m_thread.joinable())
        m_thread.join();
    {
        std::lock_guard<std::mutex> lock(api);
        m_windows.clear();
        m_screens.clear();
        if (m_keyboard)
            wl_keyboard_release(m_keyboard);
        if (m_pointer)
            wl_pointer_release(m_pointer);
        if (m_seat)
            wl_seat_destroy(m_seat);
        if (m_xkbState)
            xkb_state_unref(m_xkbState);
        if (m_xkbKeymap)
            xkb_keymap_unref(m_xkbKeymap);
        if (m_xkbContext)
            xkb_context_unref(m_xkbContext);
        if (m_wmBase)
            xdg_wm_base_destroy(m_wmBase);
        if (m_shm)
            wl_shm_destroy(m_shm);
        if (m_compositor)
            wl_compositor_destroy(m_compositor);
        m_clientRegistry.reset();
        m_registry = nullptr;
    }
    m_clientDisplay.reset();
    m_display = nullptr;
}

void WaylandBackend::onGlobal(void* data, wl_registry* registry,
                              std::uint32_t name, const char* interface,
                              std::uint32_t version) {
    auto* self = static_cast<WaylandBackend*>(data);
    if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
        self->m_compositor = static_cast<wl_compositor*>(wl_registry_bind(
            registry, name, &wl_compositor_interface, std::min(version, 4u)));
    } else if (std::strcmp(interface, wl_shm_interface.name) == 0) {
        self->m_shm = static_cast<wl_shm*>(
            wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0) {
        self->m_wmBase = static_cast<xdg_wm_base*>(
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
        xdg_wm_base_add_listener(self->m_wmBase, &wmBaseListener, self);
    } else if (std::strcmp(interface, wl_output_interface.name) == 0) {
        self->m_screens.push_back(
            std::make_unique<WaylandScreen>(self, registry, name, version));
    } else if (std::strcmp(interface, wl_seat_interface.name) == 0 &&
               !self->m_seat) {
        self->bindSeat(registry, name, version);
    }
}

void WaylandBackend::bindSeat(wl_registry* registry, std::uint32_t name,
                              std::uint32_t version) {
    m_seat = static_cast<wl_seat*>(
        wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 5u)));
    wl_seat_add_listener(m_seat, &seatListener, this);
}

void WaylandBackend::onSeatCapabilities(void* data, wl_seat*,
                                        std::uint32_t capabilities) {
    auto* self = static_cast<WaylandBackend*>(data);
    const bool hasPointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
    if (hasPointer && !self->m_pointer) {
        self->m_pointer = wl_seat_get_pointer(self->m_seat);
        wl_pointer_add_listener(self->m_pointer, &pointerListener, self);
    } else if (!hasPointer && self->m_pointer) {
        wl_pointer_release(self->m_pointer);
        self->m_pointer = nullptr;
    }
    const bool hasKeyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
    if (hasKeyboard && !self->m_keyboard) {
        self->m_keyboard = wl_seat_get_keyboard(self->m_seat);
        wl_keyboard_add_listener(self->m_keyboard, &keyboardListener, self);
    } else if (!hasKeyboard && self->m_keyboard) {
        wl_keyboard_release(self->m_keyboard);
        self->m_keyboard = nullptr;
    }
}

WaylandWindow* WaylandBackend::windowFor(wl_surface* surface) {
    for (const auto& window : m_windows)
        if (window->m_surface == surface)
            return window.get();
    return nullptr;
}

void WaylandBackend::onPointerEnter(void* data, wl_pointer*, std::uint32_t,
                                    wl_surface* surface, wl_fixed_t sx,
                                    wl_fixed_t sy) {
    auto* self = static_cast<WaylandBackend*>(data);
    self->m_pointerPos = Point{static_cast<float>(wl_fixed_to_double(sx)),
                               static_cast<float>(wl_fixed_to_double(sy))};
    // Window-local == screen-local here (a Wayland toplevel does not know its
    // global position; Platform routes by the window geometry 0,0-based).
    (void)self->windowFor(surface);
}

void WaylandBackend::onPointerLeave(void*, wl_pointer*, std::uint32_t,
                                    wl_surface*) {}

void WaylandBackend::onPointerMotion(void* data, wl_pointer*, std::uint32_t,
                                     wl_fixed_t sx, wl_fixed_t sy) {
    auto* self = static_cast<WaylandBackend*>(data);
    self->m_pointerPos = Point{static_cast<float>(wl_fixed_to_double(sx)),
                               static_cast<float>(wl_fixed_to_double(sy))};
    self->emitMouse(MouseEvent::Action::Move, self->m_pointerPos, 0);
}

void WaylandBackend::onPointerAxis(void* data, wl_pointer*, std::uint32_t,
                                   std::uint32_t axis, wl_fixed_t value) {
    auto* self = static_cast<WaylandBackend*>(data);
    // Compositors report axis scroll in wl_fixed units (10 per wheel detent
    // in practice), positive = down/right; MouseEvent::delta is in detents,
    // positive = up/right.
    const float steps = static_cast<float>(wl_fixed_to_double(value)) / 10.0f;
    if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        self->emitWheel(self->m_pointerPos, 0.0f, -steps);
    else if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL)
        self->emitWheel(self->m_pointerPos, steps, 0.0f);
}

void WaylandBackend::onPointerButton(void* data, wl_pointer*, std::uint32_t,
                                     std::uint32_t, std::uint32_t button,
                                     std::uint32_t state) {
    auto* self = static_cast<WaylandBackend*>(data);
    int mapped = 0;
    switch (button) { // linux input-event-codes
    case 0x110: mapped = 1; break; // BTN_LEFT
    case 0x111: mapped = 2; break; // BTN_RIGHT
    case 0x112: mapped = 3; break; // BTN_MIDDLE
    default: return;
    }
    self->emitMouse(state == WL_POINTER_BUTTON_STATE_PRESSED
                        ? MouseEvent::Action::Press
                        : MouseEvent::Action::Release,
                    self->m_pointerPos, mapped);
}

const wl_seat_listener WaylandBackend::seatListener = {onSeatCapabilities,
                                                       onSeatName};
const wl_pointer_listener WaylandBackend::pointerListener = {
    onPointerEnter,
    onPointerLeave,
    onPointerMotion,
    onPointerButton,
    onPointerAxis,
    [](void*, wl_pointer*) {},                         // frame
    [](void*, wl_pointer*, std::uint32_t) {},          // axis_source
    [](void*, wl_pointer*, std::uint32_t, std::uint32_t) {}, // axis_stop
    [](void*, wl_pointer*, std::uint32_t, std::int32_t) {},  // axis_discrete
    [](void*, wl_pointer*, std::uint32_t, std::int32_t) {},  // axis_value120
    [](void*, wl_pointer*, std::uint32_t, std::uint32_t) {}, // axis_relative_direction
};

void WaylandBackend::onGlobalRemove(void* data, wl_registry*,
                                    std::uint32_t name) {
    auto* self = static_cast<WaylandBackend*>(data);
    for (auto it = self->m_screens.begin(); it != self->m_screens.end(); ++it) {
        if ((*it)->registryName() == name) {
            WaylandScreen* screen = it->get();
            // The pointer is only compared (never dereferenced) by the
            // Platform after the callback, like in the headless backend.
            if (screen->announced() && self->screenCallback)
                self->screenCallback(screen, false);
            self->m_screens.erase(it);
            return;
        }
    }
}

/// Canonical multithreaded read loop: prepare_read + poll + read_events,
/// then dispatch. The 100 ms poll timeout bounds the shutdown latency.
void WaylandBackend::threadMain() {
    const int fd = m_clientDisplay->fd();
    while (m_running) {
        {
            std::lock_guard<std::mutex> lock(api);
            while (m_clientDisplay->prepareRead() != 0)
                m_clientDisplay->dispatchPending();
            m_clientDisplay->flush();
        }
        pollfd pfd{fd, POLLIN, 0};
        const int ret = ::poll(&pfd, 1, 100);
        std::lock_guard<std::mutex> lock(api);
        if (ret > 0 && (pfd.revents & POLLIN))
            m_clientDisplay->readEvents();
        else
            m_clientDisplay->cancelRead();
        const int dispatched = m_clientDisplay->dispatchPending();
        if (debugTrace() && dispatched > 0)
            trace("dispatch: ret=%d revents=0x%x dispatched=%d", ret,
                  pfd.revents, dispatched);
        // A protocol error (or the compositor going away) kills the
        // connection; stop instead of spinning on a dead fd.
        if (m_clientDisplay->lastError() != 0 ||
            (ret > 0 && (pfd.revents & (POLLERR | POLLHUP)))) {
            std::fprintf(stderr,
                         "firmir-wayland: connection lost (error=%d), "
                         "dispatch loop exits\n",
                         m_clientDisplay->lastError());
            break;
        }
    }
}

void WaylandScreen::onDone(void* data, wl_output*) {
    auto* self = static_cast<WaylandScreen*>(data);
    self->m_backend->announceScreen(self);
}

// --------------------------- WaylandWindow impl ---------------------------

WaylandWindow::WaylandWindow(WaylandBackend* backend) : m_backend(backend) {
    m_buffers[0].owner = this;
    m_buffers[1].owner = this;
    m_surface = wl_compositor_create_surface(backend->m_compositor);
    m_xdgSurface = xdg_wm_base_get_xdg_surface(backend->m_wmBase, m_surface);
    xdg_surface_add_listener(m_xdgSurface, &surfaceListener, this);
    m_toplevel = xdg_surface_get_toplevel(m_xdgSurface);
    xdg_toplevel_add_listener(m_toplevel, &toplevelListener, this);
}

void WaylandWindow::onSurfaceConfigure(void* data, xdg_surface* surface,
                                       std::uint32_t serial) {
    // Dispatch thread, api held. Buffers may only be committed after the
    // first configure; flush a frame that arrived early.
    auto* self = static_cast<WaylandWindow*>(data);
    xdg_surface_ack_configure(surface, serial);
    self->m_configured = true;
    // From this ack on, commits must match the size from the preceding
    // xdg_toplevel.configure ({0,0} = no constraint).
    self->m_requiredSize = self->m_pendingSize;
    self->m_pendingSize = Size{0, 0};
    trace("configure serial=%u pending=%d visible=%d required=%dx%d", serial,
          self->m_pending != nullptr, self->m_visible, self->m_requiredSize.width,
          self->m_requiredSize.height);
    if (self->m_pending && self->m_visible)
        self->commitBuffer(*self->m_pending);
}

void WaylandWindow::onToplevelClose(void*, xdg_toplevel*) {
    // v1: closing the window quits the application event loop.
    if (verbose())
        std::fprintf(stderr, "firmir-wayland: window closed, quitting\n");
    Firmir::Thread::mainThread()->quit();
}

void WaylandWindow::onBufferRelease(void* data, wl_buffer* handle) {
    // Runs on the dispatch thread with the backend api mutex held (all buffer
    // state is guarded by it).
    auto* buffer = static_cast<Buffer*>(data);
    buffer->busy = false;
    trace("buffer-release buf=%p", static_cast<void*>(handle));
    buffer->owner->m_backend->bufferReleased.notify_all();
}

void WaylandWindow::onFrameProbeDone(void* data, wl_callback* callback,
                                     std::uint32_t time) {
    // Dispatch thread, api held. `time` is the compositor's presentation
    // timestamp (ms); latency is measured against our commit.
    auto* probe = static_cast<FrameProbe*>(data);
    WaylandWindow* self = probe->owner;
    trace("frame-done seq=%llu time=%u latency=%.3fms%s",
          static_cast<unsigned long long>(probe->seq), time,
          (monoNs() - probe->commitNs) / 1e6,
          callback == probe->callback ? "" : " (mismatched callback!)");
    wl_callback_destroy(probe->callback);
    delete probe;
    self->m_frameProbe = nullptr;
}

const wl_buffer_listener WaylandWindow::bufferListener = {onBufferRelease};
const wl_callback_listener WaylandWindow::frameProbeListener = {
    onFrameProbeDone
};
const xdg_surface_listener WaylandWindow::surfaceListener = {onSurfaceConfigure};
const xdg_toplevel_listener WaylandWindow::toplevelListener = {
    onToplevelConfigure, onToplevelClose,
    [](void*, xdg_toplevel*, std::int32_t, std::int32_t) {}, // configure_bounds
    [](void*, xdg_toplevel*, wl_array*) {},                  // wm_capabilities
};

WaylandWindow::~WaylandWindow() {
    if (m_frameProbe) {
        wl_callback_destroy(m_frameProbe->callback);
        delete m_frameProbe;
        m_frameProbe = nullptr;
    }
    for (auto& buffer : m_buffers)
        destroyBuffer(buffer);
    if (m_toplevel)
        xdg_toplevel_destroy(m_toplevel);
    if (m_xdgSurface)
        xdg_surface_destroy(m_xdgSurface);
    if (m_surface)
        wl_surface_destroy(m_surface);
}

bool WaylandWindow::createBuffer(Buffer& buffer, int width, int height) {
    const std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
    char name[64];
    std::snprintf(name, sizeof(name), "/firmir-wl-%d-%d", ::getpid(),
                  m_shmCounter++);
    const int fd = ::shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        std::fprintf(stderr, "firmir-wayland: shm_open failed\n");
        return false;
    }
    ::shm_unlink(name); // anonymous after the unlink; the fd stays valid
    if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
        ::close(fd);
        return false;
    }
    void* data =
        ::mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        ::close(fd);
        return false;
    }
    wl_shm_pool* pool =
        wl_shm_create_pool(m_backend->m_shm, fd, static_cast<int>(bytes));
    buffer.handle = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4,
                                              WL_SHM_FORMAT_ARGB8888);
    wl_buffer_add_listener(buffer.handle, &bufferListener, &buffer);
    wl_shm_pool_destroy(pool); // the buffer keeps the memory alive
    buffer.data = static_cast<std::uint32_t*>(data);
    buffer.width = width;
    buffer.height = height;
    buffer.bytes = bytes;
    buffer.fd = fd;
    buffer.busy = false;
    return true;
}

void WaylandWindow::destroyBuffer(Buffer& buffer) {
    if (buffer.handle)
        wl_buffer_destroy(buffer.handle);
    if (buffer.data)
        ::munmap(buffer.data, buffer.bytes);
    if (buffer.fd >= 0)
        ::close(buffer.fd);
    buffer = Buffer{};
    buffer.owner = this;
}

void WaylandWindow::present(const std::uint32_t* pixels, const Size& size) {
    std::unique_lock<std::mutex> lock(m_backend->api);
    if (!m_surface || !pixels || size.width <= 0 || size.height <= 0)
        return;

    if (debugTrace()) {
        const std::uint64_t now = monoNs();
        if (m_lastPresentNs)
            trace("present: dt=%.3fms", (now - m_lastPresentNs) / 1e6);
        m_lastPresentNs = now;
    }

    Buffer* buf = nullptr;
    for (auto& b : m_buffers)
        if (!b.busy && b.width == size.width && b.height == size.height) {
            buf = &b;
            break;
        }
    if (!buf) // reuse a free buffer of the wrong size
        for (auto& b : m_buffers)
            if (!b.busy) {
                destroyBuffer(b);
                if (!createBuffer(b, size.width, size.height))
                    return;
                buf = &b;
                break;
            }
    if (!buf) {
        // Both buffers are held by the compositor: wait for a release
        // (wait_for releases the api mutex so the dispatch thread can run).
        const std::uint64_t waitStart = monoNs();
        m_backend->bufferReleased.wait_for(lock, std::chrono::milliseconds(100));
        for (auto& b : m_buffers)
            if (!b.busy) {
                if (b.width != size.width || b.height != size.height) {
                    destroyBuffer(b);
                    if (!createBuffer(b, size.width, size.height))
                        return;
                }
                buf = &b;
                break;
            }
        if (debugTrace())
            trace("present: waited %.3fms for free buffer (%s)",
                  (monoNs() - waitStart) / 1e6, buf ? "ok" : "timeout");
    }
    if (!buf) {
        trace("present: frame dropped, no free buffer");
        return; // frame dropped
    }

    const std::uint64_t copyStart = monoNs();
    std::memcpy(buf->data, pixels, buf->bytes);
    if (debugTrace())
        trace("present: memcpy %.3fms", (monoNs() - copyStart) / 1e6);
    m_pending = buf;
    if (m_configured && m_visible)
        commitBuffer(*buf);
    else
        trace("present: commit deferred (configured=%d visible=%d)",
              m_configured, m_visible);
    if (!m_loggedFirstFrame) {
        m_loggedFirstFrame = true;
        std::fprintf(stderr, "firmir-wayland: first frame presented (%dx%d)\n",
                     size.width, size.height);
    }
}

void WaylandWindow::commitBuffer(Buffer& buffer) {
    if (m_requiredSize.width > 0 &&
        (buffer.width != m_requiredSize.width ||
         buffer.height != m_requiredSize.height)) {
        // The acked configure demands another size (e.g. maximized): the GUI
        // thread applies the new geometry asynchronously and re-renders;
        // committing the old size would be a protocol error. Drop the frame.
        trace("commit skipped: buffer %dx%d, required %dx%d", buffer.width,
              buffer.height, m_requiredSize.width, m_requiredSize.height);
        m_pending = nullptr;
        return;
    }
    // Debug probe: sample the compositor's presentation timestamp for this
    // commit (at most one wl_surface_frame request in flight).
    bool probing = false;
    if (debugTrace() && !m_frameProbe) {
        auto* probe = new FrameProbe{this, nullptr, m_commitSeq, monoNs()};
        probe->callback = wl_surface_frame(m_surface);
        wl_callback_add_listener(probe->callback, &frameProbeListener, probe);
        m_frameProbe = probe;
        probing = true;
    }
    wl_surface_attach(m_surface, buffer.handle, 0, 0);
    wl_surface_damage_buffer(m_surface, 0, 0, buffer.width, buffer.height);
    wl_surface_commit(m_surface);
    // Flush right away: the dispatch loop only flushes before entering poll(),
    // so without this a commit can sit in the outgoing buffer for up to the
    // 100ms poll timeout (measured: ~96ms extra latency per frame, locking the
    // whole pipeline to ~9fps). wl_display_flush is thread-safe.
    m_backend->m_clientDisplay->flush();
    buffer.busy = true;
    m_pending = nullptr;
    if (debugTrace())
        trace("commit seq=%llu buf=%p %dx%d%s",
              static_cast<unsigned long long>(m_commitSeq++),
              static_cast<void*>(buffer.handle), buffer.width, buffer.height,
              probing ? " probe-armed" : "");
}

} // namespace

extern "C" {

DisplayBackend* firmir_gui_create_display() {
    return &WaylandBackend::instance();
}

InputBackend* firmir_gui_create_input() {
    return &WaylandBackend::instance();
}

} // extern "C"
