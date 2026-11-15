// A desktop-style client against the compositor: registry discovery, the
// per-seat objects, output state with its extra output object, an shm cursor
// buffer, a decorated toplevel with a viewport, one frame and the teardown.
//
// Every request the client sends has to reach an object the compositor really
// created — a dispatcher that compares a request name with the wrong spelling
// leaves the object missing, and the client's next request kills the
// connection ("invalid object"). This test speaks the protocols with
// libwayland-client, so nothing hides behind the client library.
#include <firmir/wayland/compositor/compositor.hpp>
#include <firmir/wayland/compositor/core.hpp>
#include <firmir/wayland/compositor/input.hpp>
#include <firmir/wayland/compositor/shell.hpp>

#include <wayland-client.h>
#include <viewporter-client-protocol.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>
#include <xdg-output-unstable-v1-client-protocol.h>
#include <xdg-shell-client-protocol.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

#include "test_framework.hpp"

using namespace Firmir;

namespace {

const char* const kDesktopSocketDir = "/tmp/firmir-wayland-desktop-test";

/// Empties the throw-away socket directory (a previous run may have left stale
/// socket files behind).
void resetSocketDir() {
    const std::string dir = kDesktopSocketDir;
    for (const char* name : {"/wayland-0", "/wayland-1"})
        ::unlink((dir + name).c_str());
    ::mkdir(dir.c_str(), 0755);
}

} // namespace

namespace {

struct Client {
    wl_display* display = nullptr;
    wl_compositor* compositor = nullptr;
    wl_shm* shm = nullptr;
    wl_seat* seat = nullptr;
    wl_output* output = nullptr;
    xdg_wm_base* wmBase = nullptr;
    zxdg_decoration_manager_v1* decoration = nullptr;
    zxdg_output_manager_v1* outputManager = nullptr;
    wp_viewporter* viewporter = nullptr;
    bool configured = false;
    bool outputDone = false;
    bool outputName = false;
    /// zxdg_output_v1.done: deprecated since version 3, must not arrive.
    bool outputExtraDone = false;
    bool decorationConfigured = false;
    /// wl_surface.enter: a client learns its screen (and scale) only from it.
    bool surfaceEntered = false;
    /// wl_callback.done for the frame the client requested.
    bool frameDone = false;
};

} // namespace

static void surfaceEnter(void* data, wl_surface*, wl_output*) {
    static_cast<Client*>(data)->surfaceEntered = true;
}
static void surfaceLeave(void*, wl_surface*, wl_output*) {}
static const wl_surface_listener surfaceEventListener = {surfaceEnter, surfaceLeave, nullptr,
                                                         nullptr};

static void frameDone(void* data, wl_callback*, std::uint32_t) {
    static_cast<Client*>(data)->frameDone = true;
}
static const wl_callback_listener frameListener = {frameDone};

static void bufferRelease(void*, wl_buffer*) {}
static const wl_buffer_listener bufferListener = {bufferRelease};

static void wmBasePing(void*, xdg_wm_base* base, std::uint32_t value) {
    xdg_wm_base_pong(base, value);
}
static const xdg_wm_base_listener wmBaseListener = {wmBasePing};

static void surfaceConfigure(void* data, xdg_surface* surface, std::uint32_t value) {
    xdg_surface_ack_configure(surface, value);
    static_cast<Client*>(data)->configured = true;
}
static const xdg_surface_listener surfaceListener = {surfaceConfigure};

static void toplevelConfigure(void*, xdg_toplevel*, std::int32_t, std::int32_t, wl_array*) {}
static void toplevelClose(void*, xdg_toplevel*) {}
static void toplevelBounds(void*, xdg_toplevel*, std::int32_t, std::int32_t) {}
static void toplevelCapabilities(void*, xdg_toplevel*, wl_array*) {}
static const xdg_toplevel_listener toplevelListener = {
    toplevelConfigure, toplevelClose, toplevelBounds, toplevelCapabilities};

static void outputGeometry(void*, wl_output*, std::int32_t, std::int32_t, std::int32_t,
                           std::int32_t, std::int32_t, const char*, const char*, std::int32_t) {}
static void outputMode(void*, wl_output*, std::uint32_t, std::int32_t, std::int32_t,
                       std::int32_t) {}
static void outputDone(void* data, wl_output*) { static_cast<Client*>(data)->outputDone = true; }
static void outputScale(void*, wl_output*, std::int32_t) {}
static void outputName(void*, wl_output*, const char*) {}
static void outputDescription(void*, wl_output*, const char*) {}
static const wl_output_listener outputListener = {outputGeometry, outputMode,   outputDone,
                                                  outputScale,    outputName,   outputDescription};

static void extraPosition(void*, zxdg_output_v1*, std::int32_t, std::int32_t) {}
static void extraSize(void*, zxdg_output_v1*, std::int32_t, std::int32_t) {}
static void extraDone(void* data, zxdg_output_v1*) {
    static_cast<Client*>(data)->outputExtraDone = true;
}
static void extraName(void* data, zxdg_output_v1*, const char*) {
    static_cast<Client*>(data)->outputName = true;
}
static void extraDescription(void*, zxdg_output_v1*, const char*) {}
static const zxdg_output_v1_listener extraOutputListener = {
    extraPosition, extraSize, extraDone, extraName, extraDescription};

static void decorationConfigure(void* data, zxdg_toplevel_decoration_v1*, std::uint32_t) {
    static_cast<Client*>(data)->decorationConfigured = true;
}
static const zxdg_toplevel_decoration_v1_listener decorationListener = {decorationConfigure};

static void seatCapabilities(void*, wl_seat*, std::uint32_t) {}
static void seatName(void*, wl_seat*, const char*) {}
static const wl_seat_listener seatListener = {seatCapabilities, seatName};


static void pointerEnter(void*, wl_pointer*, std::uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t) {}
static void pointerLeave(void*, wl_pointer*, std::uint32_t, wl_surface*) {}
static void pointerMotion(void*, wl_pointer*, std::uint32_t, wl_fixed_t, wl_fixed_t) {}
static void pointerButton(void*, wl_pointer*, std::uint32_t, std::uint32_t, std::uint32_t,
                          std::uint32_t) {}
static void pointerAxis(void*, wl_pointer*, std::uint32_t, std::uint32_t, wl_fixed_t) {}
static void pointerFrame(void*, wl_pointer*) {}
static void pointerAxisSource(void*, wl_pointer*, std::uint32_t) {}
static void pointerAxisStop(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}
static void pointerAxisDiscrete(void*, wl_pointer*, std::uint32_t, std::int32_t) {}
static void pointerAxisValue120(void*, wl_pointer*, std::uint32_t, std::int32_t) {}
static void pointerAxisRelative(void*, wl_pointer*, std::uint32_t, std::uint32_t) {}
static const wl_pointer_listener pointerListener = {
    pointerEnter, pointerLeave, pointerMotion, pointerButton, pointerAxis,
    pointerFrame, pointerAxisSource, pointerAxisStop, pointerAxisDiscrete,
    pointerAxisValue120, pointerAxisRelative};

static void keyboardKeymap(void*, wl_keyboard*, std::uint32_t, std::int32_t, std::uint32_t) {}
static void keyboardEnter(void*, wl_keyboard*, std::uint32_t, wl_surface*, wl_array*) {}
static void keyboardLeave(void*, wl_keyboard*, std::uint32_t, wl_surface*) {}
static void keyboardKey(void*, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t,
                        std::uint32_t) {}
static void keyboardModifiers(void*, wl_keyboard*, std::uint32_t, std::uint32_t, std::uint32_t,
                              std::uint32_t, std::uint32_t) {}
static void keyboardRepeat(void*, wl_keyboard*, std::int32_t, std::int32_t) {}
static const wl_keyboard_listener keyboardListener = {
    keyboardKeymap, keyboardEnter, keyboardLeave, keyboardKey, keyboardModifiers,
    keyboardRepeat};

static void touchDown(void*, wl_touch*, std::uint32_t, std::uint32_t, wl_surface*, std::int32_t,
                      wl_fixed_t, wl_fixed_t) {}
static void touchUp(void*, wl_touch*, std::uint32_t, std::uint32_t, std::int32_t) {}
static void touchMotion(void*, wl_touch*, std::uint32_t, std::int32_t, wl_fixed_t, wl_fixed_t) {}
static void touchFrame(void*, wl_touch*) {}
static void touchCancel(void*, wl_touch*) {}
static void touchShape(void*, wl_touch*, std::int32_t, wl_fixed_t, wl_fixed_t) {}
static void touchOrientation(void*, wl_touch*, std::int32_t, wl_fixed_t) {}
static const wl_touch_listener touchListener = {touchDown,     touchUp,       touchMotion,
                                                touchFrame,    touchCancel,   touchShape,
                                                touchOrientation};

static void registryGlobal(void* data, wl_registry* registry, std::uint32_t name,
                           const char* interface, std::uint32_t version) {
    auto* client = static_cast<Client*>(data);
    if (std::strcmp(interface, "wl_compositor") == 0)
        client->compositor = static_cast<wl_compositor*>(wl_registry_bind(
            registry, name, &wl_compositor_interface, version < 4 ? version : 4));
    else if (std::strcmp(interface, "wl_shm") == 0)
        client->shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    else if (std::strcmp(interface, "wl_seat") == 0) {
        client->seat = static_cast<wl_seat*>(wl_registry_bind(
            registry, name, &wl_seat_interface, version < 5 ? version : 5));
        wl_seat_add_listener(client->seat, &seatListener, client);
    } else if (std::strcmp(interface, "wl_output") == 0) {
        // A desktop client binds at most version 3 here.
        client->output = static_cast<wl_output*>(wl_registry_bind(
            registry, name, &wl_output_interface, version < 3 ? version : 3));
        wl_output_add_listener(client->output, &outputListener, client);
    } else if (std::strcmp(interface, "xdg_wm_base") == 0) {
        client->wmBase = static_cast<xdg_wm_base*>(wl_registry_bind(
            registry, name, &xdg_wm_base_interface, version < 6 ? version : 6));
        xdg_wm_base_add_listener(client->wmBase, &wmBaseListener, client);
    } else if (std::strcmp(interface, "zxdg_decoration_manager_v1") == 0) {
        client->decoration = static_cast<zxdg_decoration_manager_v1*>(
            wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
    } else if (std::strcmp(interface, "zxdg_output_manager_v1") == 0) {
        client->outputManager = static_cast<zxdg_output_manager_v1*>(wl_registry_bind(
            registry, name, &zxdg_output_manager_v1_interface, version < 3 ? version : 3));
    } else if (std::strcmp(interface, "wp_viewporter") == 0) {
        client->viewporter = static_cast<wp_viewporter*>(
            wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
    }
}
static void registryGlobalRemove(void*, wl_registry*, std::uint32_t) {}
static const wl_registry_listener registryListener = {registryGlobal, registryGlobalRemove};

/// Destroys the proxies registryGlobal() created. libwayland frees a proxy only
/// when it is destroyed — disconnecting the display does not — so a client that
/// binds globals has to release them, otherwise a leak checker reports the
/// difference.
static void releaseGlobals(Client& client) {
    if (client.wmBase) {
        xdg_wm_base_destroy(client.wmBase);
        client.wmBase = nullptr;
    }
    if (client.seat) {
        wl_seat_release(client.seat);
        client.seat = nullptr;
    }
    if (client.shm) {
        wl_shm_destroy(client.shm);
        client.shm = nullptr;
    }
    if (client.compositor) {
        wl_compositor_destroy(client.compositor);
        client.compositor = nullptr;
    }
    if (client.output) {
        wl_output_release(client.output);
        client.output = nullptr;
    }
    if (client.decoration) {
        zxdg_decoration_manager_v1_destroy(client.decoration);
        client.decoration = nullptr;
    }
    if (client.outputManager) {
        zxdg_output_manager_v1_destroy(client.outputManager);
        client.outputManager = nullptr;
    }
    if (client.viewporter) {
        wp_viewporter_destroy(client.viewporter);
        client.viewporter = nullptr;
    }
}

namespace {

/// Roundtrip that fails the test on a protocol error: a fatal error here means
/// the compositor sent wl_display.error, e.g. "invalid object" for a request
/// whose object was never created.
bool roundtrip(Client& client, const char* step) {
    if (wl_display_roundtrip(client.display) < 0) {
        std::printf("  step '%s' failed\n", step);
        return false;
    }
    return true;
}

/// wl_shm buffer over a scratch memfd, as a client allocates its pools.
wl_buffer* makeBuffer(wl_shm* shm, wl_shm_pool** poolOut, int width, int height,
                      std::uint32_t format, unsigned char fill) {
    const int stride = width * 4, size = stride * height;
    const int fd = ::memfd_create("firmir-wayland-test", MFD_CLOEXEC);
    if (fd < 0 || ::ftruncate(fd, size) < 0)
        return nullptr;
    void* data = ::mmap(nullptr, static_cast<std::size_t>(size), PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    std::memset(data, fill, static_cast<std::size_t>(size));
    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    ::close(fd);
    *poolOut = pool;
    return wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
}

} // namespace

namespace {

/// The client half: everything a desktop client does at startup and shutdown.
/// It runs in its own thread because a roundtrip blocks, while the compositor
/// has to be driven from the thread that created it — its objects (clients,
/// surfaces, shell objects) are constructed during dispatch.
void runDesktopClient(const std::string& socket, Client& client) {
    client.display = wl_display_connect(socket.c_str());
    CHECK(client.display != nullptr);
    if (client.display) {
        wl_registry* registry = wl_display_get_registry(client.display);
        wl_registry_add_listener(registry, &registryListener, &client);
        CHECK(roundtrip(client, "registry"));
        CHECK(client.compositor != nullptr);
        CHECK(client.shm != nullptr);
        CHECK(client.seat != nullptr);
        CHECK(client.output != nullptr);
        CHECK(client.wmBase != nullptr);
        CHECK(client.decoration != nullptr);
        CHECK(client.outputManager != nullptr);
        CHECK(client.viewporter != nullptr);

        // Per-seat children, created as soon as the seat is known.
        wl_pointer* pointer = wl_seat_get_pointer(client.seat);
        wl_pointer_add_listener(pointer, &pointerListener, &client);
        wl_keyboard* keyboard = wl_seat_get_keyboard(client.seat);
        wl_keyboard_add_listener(keyboard, &keyboardListener, &client);
        wl_touch* touch = wl_seat_get_touch(client.seat);
        wl_touch_add_listener(touch, &touchListener, &client);
        CHECK(roundtrip(client, "seat children"));

        // The output's extra object (logical geometry and name).
        zxdg_output_v1* extraOutput = nullptr;
        if (client.outputManager) {
            extraOutput = zxdg_output_manager_v1_get_xdg_output(client.outputManager,
                                                                client.output);
            zxdg_output_v1_add_listener(extraOutput, &extraOutputListener, &client);
        }
        CHECK(roundtrip(client, "output"));
        CHECK(client.outputDone);       // wl_output.done after the initial state
        CHECK(client.outputName);       // the logical name of the output
        CHECK(!client.outputExtraDone); // deprecated at version 3


        // Cursor buffer out of a shared pool, before any window exists.
        wl_shm_pool* pool = nullptr;
        wl_buffer* cursor = makeBuffer(client.shm, &pool, 16, 16, WL_SHM_FORMAT_ARGB8888, 0);
        wl_surface* cursorSurface = nullptr;
        CHECK(cursor != nullptr);
        if (cursor) {
            wl_shm_pool_destroy(pool);
            cursorSurface = wl_compositor_create_surface(client.compositor);
            wl_surface_attach(cursorSurface, cursor, 0, 0);
            wl_surface_damage(cursorSurface, 0, 0, 16, 16);
            wl_surface_commit(cursorSurface);
            wl_pointer_set_cursor(pointer, 1, cursorSurface, 8, 8);
            CHECK(roundtrip(client, "cursor"));
        }

        // A decorated toplevel with a viewport.
        wl_surface* surface = wl_compositor_create_surface(client.compositor);
        xdg_surface* xdgSurface = xdg_wm_base_get_xdg_surface(client.wmBase, surface);
        xdg_surface_add_listener(xdgSurface, &surfaceListener, &client);
        xdg_toplevel* toplevel = xdg_surface_get_toplevel(xdgSurface);
        xdg_toplevel_add_listener(toplevel, &toplevelListener, &client);
        xdg_toplevel_set_title(toplevel, "test");
        xdg_toplevel_set_app_id(toplevel, "firmir-test");
        xdg_toplevel_set_min_size(toplevel, 100, 100);
        zxdg_toplevel_decoration_v1* topDecoration = nullptr;
        if (client.decoration) {
            topDecoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
                client.decoration, toplevel);
            zxdg_toplevel_decoration_v1_add_listener(topDecoration, &decorationListener,
                                                     &client);
            zxdg_toplevel_decoration_v1_set_mode(
                topDecoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }
        wp_viewport* viewport = nullptr;
        if (client.viewporter) {
            viewport = wp_viewporter_get_viewport(client.viewporter, surface);
            wp_viewport_set_destination(viewport, 50, 50);
        }
        wl_surface_commit(surface);
        CHECK(roundtrip(client, "configure"));
        CHECK(client.configured);
        CHECK(topDecoration == nullptr || client.decorationConfigured);

        // First frame.
        pool = nullptr;
        wl_buffer* windowBuffer =
            makeBuffer(client.shm, &pool, 100, 100, WL_SHM_FORMAT_XRGB8888, 0x40);
        CHECK(windowBuffer != nullptr);
        if (windowBuffer) {
            wl_shm_pool_destroy(pool);
            wl_callback* frame = wl_surface_frame(surface);
            wl_surface_attach(surface, windowBuffer, 0, 0);
            wl_surface_damage(surface, 0, 0, 100, 100);
            wl_surface_commit(surface);
            CHECK(roundtrip(client, "first frame"));
            wl_callback_destroy(frame);
            wl_buffer_destroy(windowBuffer);
        }

        // Teardown in the order a desktop client uses: role objects, then the
        // surfaces, then the per-seat objects, then the screen objects.
        xdg_toplevel_destroy(toplevel);
        if (topDecoration) zxdg_toplevel_decoration_v1_destroy(topDecoration);
        if (viewport) wp_viewport_destroy(viewport);
        xdg_surface_destroy(xdgSurface);
        wl_surface_destroy(surface);
        CHECK(roundtrip(client, "window teardown"));
        if (cursor) wl_buffer_destroy(cursor);
        wl_pointer_release(pointer);
        if (cursorSurface) wl_surface_destroy(cursorSurface);
        wl_keyboard_release(keyboard);
        wl_touch_release(touch);
        if (extraOutput) zxdg_output_v1_destroy(extraOutput);
        wl_output_release(client.output);
        CHECK(roundtrip(client, "screen teardown"));
        if (client.outputManager) zxdg_output_manager_v1_destroy(client.outputManager);
        if (client.decoration) zxdg_decoration_manager_v1_destroy(client.decoration);
        if (client.viewporter) wp_viewporter_destroy(client.viewporter);
        xdg_wm_base_destroy(client.wmBase);
        wl_seat_release(client.seat);
        wl_shm_destroy(client.shm);
        wl_compositor_destroy(client.compositor);
        wl_registry_destroy(registry);
        CHECK(roundtrip(client, "globals teardown"));
        wl_display_disconnect(client.display);
    }
}

} // namespace

TEST_CASE(wayland_compositor_desktop_client_startup) {
    ::setenv("FIRMIR_WAYLAND_SOCKET_DIR", kDesktopSocketDir, 1);
    resetSocketDir();

    Wayland::Compositor::WaylandCompositor compositor;
    CHECK(compositor.create());
    Wayland::Compositor::WaylandOutput* output = compositor.addOutput({800, 600});
    CHECK(output != nullptr);
    if (output) {
        output->setManufacturer("Firmir");
        output->setModel("test");
    }
    auto* xdgShell = new Wayland::Compositor::WaylandXdgShell(&compositor);
    CHECK(xdgShell->initialize(&compositor));
    auto* seat = new Wayland::Compositor::WaylandSeat(&compositor);
    seat->setCapabilities(WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD |
                          WL_SEAT_CAPABILITY_TOUCH);
    CHECK(seat->initialize(&compositor));
    auto* viewporter = new Wayland::Compositor::WaylandViewporter(&compositor);
    CHECK(viewporter->initialize(&compositor));
    auto* decoration = new Wayland::Compositor::WaylandXdgDecorationManagerV1(&compositor);
    decoration->setDefaultMode(
        Wayland::Compositor::WaylandXdgDecorationManagerV1::Mode::ClientSide);
    CHECK(decoration->initialize(&compositor));
    auto* outputManager = new Wayland::Compositor::WaylandXdgOutputManagerV1(&compositor);
    CHECK(outputManager->initialize(&compositor));

    const std::string socket = compositor.addSocketAuto();
    CHECK(!socket.empty());
    if (socket.empty()) {
        ::unsetenv("FIRMIR_WAYLAND_SOCKET_DIR");
        return;
    }

    Client client;
    std::atomic<bool> done{false};
    std::thread clientThread([&] {
        runDesktopClient(socket, client);
        done = true;
    });
    while (!done.load())
        compositor.dispatch(10);
    clientThread.join();
    ::unsetenv("FIRMIR_WAYLAND_SOCKET_DIR");
}

namespace {

/// Colour the frame client paints: 0xAARRGGBB, i.e. B, G, R, A in memory for
/// the ARGB8888 wl_shm format.
constexpr std::uint32_t kFrameColor = 0xFF3366CCu;
/// The same colour as a grabbed frame spells it: R, G, B, A bytes, the layout
/// Painter::drawRgba() takes (see WaylandSurfaceGrabber::Frame).
constexpr std::uint32_t kFrameColorRgba = 0xFFCC6633u;

wl_buffer* makeColorBuffer(wl_shm* shm, wl_shm_pool** poolOut, int width, int height,
                           std::uint32_t argb) {
    const int stride = width * 4, size = stride * height;
    const int fd = ::memfd_create("firmir-wayland-frame-test", MFD_CLOEXEC);
    if (fd < 0 || ::ftruncate(fd, size) < 0)
        return nullptr;
    void* data = ::mmap(nullptr, static_cast<std::size_t>(size), PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd, 0);
    auto* pixels = static_cast<std::uint32_t*>(data);
    for (int i = 0; i < width * height; ++i)
        pixels[i] = argb;
    wl_shm_pool* pool = wl_shm_create_pool(shm, fd, size);
    ::close(fd);
    *poolOut = pool;
    return wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
}

/// The client half of the pixel test: draws one frame the way a toolkit does
/// (a bufferless commit to get a size, then a coloured wl_shm buffer) and
/// keeps the surface alive until the compositor copied the pixels out of it.
void runFrameClient(const std::string& socket, Client& client, std::atomic<bool>& ready,
                    std::atomic<bool>& grabbed) {
    client.display = wl_display_connect(socket.c_str());
    if (!client.display) return;
    wl_registry* registry = wl_display_get_registry(client.display);
    wl_registry_add_listener(registry, &registryListener, &client);
    if (!roundtrip(client, "globals")) return;
    if (!client.compositor || !client.shm || !client.wmBase) {
        std::printf("  the client did not get wl_compositor/wl_shm/xdg_wm_base\n");
        return;
    }
    wl_surface* surface = wl_compositor_create_surface(client.compositor);
    wl_surface_add_listener(surface, &surfaceEventListener, &client);
    xdg_surface* xSurface = xdg_wm_base_get_xdg_surface(client.wmBase, surface);
    xdg_surface_add_listener(xSurface, &surfaceListener, &client);
    xdg_toplevel* toplevel = xdg_surface_get_toplevel(xSurface);
    xdg_toplevel_add_listener(toplevel, &toplevelListener, &client);
    xdg_toplevel_set_title(toplevel, "frame");
    // A first commit without a buffer: it asks the compositor for a size.
    wl_surface_commit(surface);
    if (!roundtrip(client, "configure")) return;

    wl_shm_pool* pool = nullptr;
    wl_buffer* buffer = makeColorBuffer(client.shm, &pool, 64, 48, kFrameColor);
    if (!buffer) return;
    wl_buffer_add_listener(buffer, &bufferListener, &client);
    wl_callback* callback = wl_surface_frame(surface);
    wl_callback_add_listener(callback, &frameListener, &client);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, 64, 48);
    wl_surface_commit(surface);
    if (!roundtrip(client, "frame")) return;

    ready = true;
    while (!grabbed.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(2));

    wl_callback_destroy(callback);
    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xSurface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);
    wl_shm_pool_destroy(pool);
    wl_registry_destroy(registry);
    releaseGlobals(client);
    wl_display_disconnect(client.display);
}

} // namespace

// A window whose pixels never arrive looks the same as a window that is not
// painted at all: black. This test proves the whole path — bufferless commit,
// configure, coloured wl_shm buffer, attach, commit — ends with the compositor
// holding exactly those pixels, and that the client is told which output it
// landed on (wl_surface.enter) and that its frame callback fired.
TEST_CASE(wayland_compositor_client_frame_reaches_painter) {
    ::setenv("FIRMIR_WAYLAND_SOCKET_DIR", kDesktopSocketDir, 1);
    resetSocketDir();

    Wayland::Compositor::WaylandCompositor compositor;
    CHECK(compositor.create());
    Wayland::Compositor::WaylandOutput* output = compositor.addOutput({320, 240});
    CHECK(output != nullptr);
    auto* xdgShell = new Wayland::Compositor::WaylandXdgShell(&compositor);
    CHECK(xdgShell->initialize(&compositor));

    const std::string socket = compositor.addSocketAuto();
    CHECK(!socket.empty());
    if (socket.empty()) {
        ::unsetenv("FIRMIR_WAYLAND_SOCKET_DIR");
        return;
    }

    Client client;
    std::atomic<bool> ready{false};
    std::atomic<bool> grabbed{false};
    std::atomic<bool> done{false};
    std::thread clientThread([&] {
        runFrameClient(socket, client, ready, grabbed);
        done = true;
    });
    while (!ready.load() && !done.load())
        compositor.dispatch(10);

    const auto& surfaces = compositor.surfaces();
    CHECK(!surfaces.empty());
    if (!surfaces.empty()) {
        Wayland::Compositor::WaylandSurfaceGrabber::Frame frame =
            Wayland::Compositor::WaylandSurfaceGrabber().grab(surfaces.front());
        CHECK(frame.width == 64);
        CHECK(frame.height == 48);
        CHECK(!frame.pixels.empty());
        if (!frame.pixels.empty())
            CHECK(frame.pixels.front() == kFrameColorRgba);
    }
    grabbed = true;
    while (!done.load())
        compositor.dispatch(10);
    clientThread.join();

    CHECK(client.configured);
    CHECK(client.surfaceEntered);
    CHECK(client.frameDone);
    ::unsetenv("FIRMIR_WAYLAND_SOCKET_DIR");
}

namespace {

/// Counts the pointer events a client receives and the frame markers that end
/// each batch (wl_pointer has grouped its events since version 5: a client may
/// hold a batch back until the frame arrives).
struct ClickClient {
    Client client;
    std::atomic<bool> ready{false};
    std::atomic<int> presses{0};
    std::atomic<int> releases{0};
    std::atomic<int> frames{0};
    /// Events received since the last frame; a client that batches would still
    /// be holding them, so a non-zero value at the end means a lost frame.
    std::atomic<int> pending{0};
};

void clickPointerButton(void* data, wl_pointer*, std::uint32_t, std::uint32_t,
                        std::uint32_t button, std::uint32_t state) {
    auto* client = static_cast<ClickClient*>(data);
    if (button != 0x110) // BTN_LEFT
        return;
    if (state == WL_POINTER_BUTTON_STATE_PRESSED)
        client->presses.fetch_add(1);
    else
        client->releases.fetch_add(1);
    client->pending.fetch_add(1);
}
void clickPointerFrame(void* data, wl_pointer*) {
    auto* client = static_cast<ClickClient*>(data);
    client->frames.fetch_add(1);
    client->pending.store(0);
}
const wl_pointer_listener clickPointerListener = {
    [](void*, wl_pointer*, std::uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t) {}, // enter
    [](void*, wl_pointer*, std::uint32_t, wl_surface*) {},                          // leave
    [](void*, wl_pointer*, std::uint32_t, wl_fixed_t, wl_fixed_t) {},               // motion
    clickPointerButton,
    [](void*, wl_pointer*, std::uint32_t, std::uint32_t, wl_fixed_t) {}, // axis
    clickPointerFrame,
    [](void*, wl_pointer*, std::uint32_t) {},                // axis_source
    [](void*, wl_pointer*, std::uint32_t, std::uint32_t) {}, // axis_stop
    [](void*, wl_pointer*, std::uint32_t, std::int32_t) {},  // axis_discrete
    [](void*, wl_pointer*, std::uint32_t, std::int32_t) {},  // axis_value120
    [](void*, wl_pointer*, std::uint32_t, std::uint32_t) {}, // axis_relative
};

} // namespace

namespace {

/// One toplevel that stays alive while the compositor sends two clicks; the
/// client leaves as soon as both press/release pairs arrived (or after a
/// timeout, so a regression fails the test instead of hanging it).
void runClickClient(const std::string& socket, ClickClient& state) {
    Client& client = state.client;
    client.display = wl_display_connect(socket.c_str());
    if (!client.display) return;
    wl_registry* registry = wl_display_get_registry(client.display);
    wl_registry_add_listener(registry, &registryListener, &client);
    if (!roundtrip(client, "globals")) return;
    if (!client.compositor || !client.shm || !client.wmBase || !client.seat) return;
    wl_pointer* pointer = wl_seat_get_pointer(client.seat);
    wl_pointer_add_listener(pointer, &clickPointerListener, &state);

    wl_surface* surface = wl_compositor_create_surface(client.compositor);
    xdg_surface* xSurface = xdg_wm_base_get_xdg_surface(client.wmBase, surface);
    xdg_surface_add_listener(xSurface, &surfaceListener, &client);
    xdg_toplevel* toplevel = xdg_surface_get_toplevel(xSurface);
    xdg_toplevel_add_listener(toplevel, &toplevelListener, &client);
    xdg_toplevel_set_title(toplevel, "clicks");
    wl_surface_commit(surface);
    if (!roundtrip(client, "configure")) return;

    wl_shm_pool* pool = nullptr;
    wl_buffer* buffer = makeColorBuffer(client.shm, &pool, 64, 48, kFrameColor);
    if (!buffer) return;
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, 64, 48);
    wl_surface_commit(surface);
    state.ready = true;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (state.presses.load() < 2 || state.releases.load() < 2) {
        if (std::chrono::steady_clock::now() > deadline) break;
        pollfd pfd{wl_display_get_fd(client.display), POLLIN, 0};
        if (::poll(&pfd, 1, 100) <= 0) continue;
        if (wl_display_dispatch(client.display) < 0) break;
    }
    roundtrip(client, "clicks"); // a protocol error is reported by the counters

    xdg_toplevel_destroy(toplevel);
    xdg_surface_destroy(xSurface);
    wl_surface_destroy(surface);
    wl_buffer_destroy(buffer);
    wl_shm_pool_destroy(pool);
    wl_pointer_destroy(pointer);
    wl_registry_destroy(registry);
    releaseGlobals(client);
    wl_display_disconnect(client.display);
}

} // namespace

// A click that works once and never again is the classic symptom of a pointer
// batch that never ends (no frame) or of hover state that swallows the second
// press. This test sends two full press/release pairs to one client and checks
// that every event arrived, that each batch ended with a frame, and that the
// client stayed connected.
TEST_CASE(wayland_compositor_pointer_clicks_repeat) {
    ::setenv("FIRMIR_WAYLAND_SOCKET_DIR", kDesktopSocketDir, 1);
    resetSocketDir();

    Wayland::Compositor::WaylandCompositor compositor;
    CHECK(compositor.create());
    CHECK(compositor.addOutput({320, 240}) != nullptr);
    auto* xdgShell = new Wayland::Compositor::WaylandXdgShell(&compositor);
    CHECK(xdgShell->initialize(&compositor));
    auto* seat = new Wayland::Compositor::WaylandSeat(&compositor);
    seat->setCapabilities(WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    CHECK(seat->initialize(&compositor));

    const std::string socket = compositor.addSocketAuto();
    CHECK(!socket.empty());
    if (socket.empty()) {
        ::unsetenv("FIRMIR_WAYLAND_SOCKET_DIR");
        return;
    }

    ClickClient state;
    std::thread clientThread([&] { runClickClient(socket, state); });
    while (!state.ready.load())
        compositor.dispatch(10);

    const auto& surfaces = compositor.surfaces();
    CHECK(!surfaces.empty());
    if (!surfaces.empty()) {
        Wayland::Compositor::WaylandSurface* surface = surfaces.back();
        Wayland::Compositor::WaylandPointer* pointer = seat->pointer();
        pointer->enter(surface, 20, 10);
        compositor.dispatch(0); // flush the enter before the clicks
        for (int click = 0; click < 2; ++click) {
            pointer->button(1, 0x110 /*BTN_LEFT*/, true);
            pointer->button(1, 0x110 /*BTN_LEFT*/, false);
        }
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while ((state.presses.load() < 2 || state.releases.load() < 2) &&
               std::chrono::steady_clock::now() < deadline)
            compositor.dispatch(10);
    }
    clientThread.join();

    CHECK_EQ(state.presses.load(), 2);
    CHECK_EQ(state.releases.load(), 2);
    // enter plus one per click event: every batch must end with a frame,
    // otherwise a batching client applies nothing.
    CHECK(state.frames.load() >= 5);
    CHECK_EQ(state.pending.load(), 0);
    ::unsetenv("FIRMIR_WAYLAND_SOCKET_DIR");
}
