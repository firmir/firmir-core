#include "test_framework.hpp"

#include <core_client.hpp>
#include <xdg_shell_client.hpp>
#include <firmir/wayland/client/backend.hpp>
#include <firmir/wayland/client/display.hpp>
#include <firmir/wayland/client/registry.hpp>
#include <firmir/wayland/compositor/bufferbackend.hpp>
#include <firmir/wayland/compositor/compositor.hpp>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace Firmir;

namespace {

const char* const kTestSocketDir = "/tmp/firmir-wayland-socket-test";

bool pathExists(const std::string& path) {
    struct stat info {};
    return ::stat(path.c_str(), &info) == 0;
}

/// Empties the throw-away socket directory (a previous run may have left
/// stale socket files behind).
void resetSocketDir() {
    const std::string dir = kTestSocketDir;
    for (const char* name : {"/wayland-0", "/wayland-1", "/custom.sock", "/absolute.sock"})
        ::unlink((dir + name).c_str());
    ::mkdir(dir.c_str(), 0755);
}

/// Restores the environment after a test that overrides the socket setup.
struct SocketEnvironmentGuard {
    ~SocketEnvironmentGuard() {
        ::unsetenv("FIRMIR_WAYLAND_SOCKET_DIR");
        ::unsetenv("FIRMIR_WAYLAND_SOCKET");
    }
};

} // namespace

TEST_CASE(wayland_generated_client_wrappers) {
    Wayland::Client::Surface surface;
    Wayland::Client::Shm shm;
    Wayland::Client::XdgWmBase wm;
    Wayland::Client::XdgSurface xdg;
    CHECK(!surface);
    CHECK(!shm);
    CHECK(!wm);
    CHECK(!xdg);
}

TEST_CASE(wayland_compositor_core_and_extensions) {
    using namespace Wayland::Compositor;
    WaylandCompositor compositor;
    CHECK(compositor.create());
    CHECK(compositor.valid());
    auto* output = compositor.addOutput({800, 600});
    CHECK(output != nullptr);
    if (output) {
        CHECK_EQ(output->size().width, 800);
        CHECK_EQ(output->size().height, 600);
        CHECK_EQ(output->currentMode().refreshRate, 60000);
    }
    auto* seat = new WaylandSeat(&compositor);
    seat->setCapabilities(WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    CHECK(seat->initialize(&compositor));
    auto* xdg = new WaylandXdgShell(&compositor);
    CHECK(xdg->initialize(&compositor));
    auto* legacy = new WaylandWlShell(&compositor);
    CHECK(legacy->initialize(&compositor));
    auto* viewporter = new WaylandViewporter(&compositor);
    CHECK(viewporter->initialize(&compositor));
    auto* decoration = new WaylandXdgDecorationManagerV1(&compositor);
    CHECK(decoration->initialize(&compositor));
    auto* outputs = new WaylandXdgOutputManagerV1(&compositor);
    CHECK(outputs->initialize(&compositor));
    WaylandBufferRef empty;
    CHECK(!empty.valid());
    CHECK(!empty.isShm());
}

// wl_output reports the physical panel size, not the pixel mode: a client lays
// out in millimetres from it. The minimize request is a one-shot the
// compositor consumes.
// The buffer backend is bound to the wl_display it was created for: the
// accelerated one binds EGL to it, which creates a wl_drm global there. If the
// backend outlived the display, its destructor would unbind through a freed
// display at process exit (a SIGSEGV inside the driver's Wayland code).
TEST_CASE(buffer_backend_is_destroyed_with_its_display) {
    using namespace Wayland::Compositor;
    auto compositor = std::make_unique<WaylandCompositor>();
    CHECK(compositor->create());
    // A backend may be missing in a tree without the plugins; the display-bound
    // lifetime is what is being checked here, so only run when one was loaded.
    if (!BufferBackends::selected()) {
        std::fprintf(stderr, "skipped: no buffer backend plugin\n");
        return;
    }
    CHECK(!BufferBackends::selectedName().empty());
    compositor.reset();
    CHECK(BufferBackends::selected() == nullptr);
}

TEST_CASE(wayland_output_physical_size_and_client_minimize) {
    using namespace Wayland::Compositor;
    WaylandCompositor compositor;
    CHECK(compositor.create());
    auto* output = compositor.addOutput({1200, 2000});
    CHECK(output != nullptr);
    if (output) {
        // Unknown until the compositor knows its panel: {0, 0} means
        // "unknown" to the client, never "1200x2000 mm".
        CHECK_EQ(output->physicalSize().width, 0);
        CHECK_EQ(output->physicalSize().height, 0);
        output->setPhysicalSize({100, 167});
        CHECK_EQ(output->physicalSize().width, 100);
        CHECK_EQ(output->physicalSize().height, 167);
        // The pixel size is unaffected.
        CHECK_EQ(output->size().width, 1200);
        CHECK_EQ(output->size().height, 2000);
    }

    WaylandXdgToplevel toplevel;
    CHECK(!toplevel.takeMinimizeRequest());
    toplevel.setMinimized();
    CHECK(toplevel.takeMinimizeRequest());
    CHECK(!toplevel.takeMinimizeRequest());
    toplevel.setMinimized();
    CHECK(toplevel.takeMinimizeRequest());
}

// Display::addSocketAuto(): /run/display/wayland-0 by default, wayland-1 when
// the first name is taken, overridable through the environment.
TEST_CASE(wayland_socket_auto_numbering) {
    using namespace Wayland::Compositor;
    SocketEnvironmentGuard guard;
    resetSocketDir();
    ::setenv("FIRMIR_WAYLAND_SOCKET_DIR", kTestSocketDir, 1);

    WaylandCompositor first;
    CHECK(first.create());
    const std::string firstSocket = first.addSocketAuto();
    CHECK_EQ(firstSocket, std::string(kTestSocketDir) + "/wayland-0");
    CHECK(pathExists(firstSocket));

    // A second compositor gets the next free name; the first one keeps its.
    WaylandCompositor second;
    CHECK(second.create());
    const std::string secondSocket = second.addSocketAuto();
    CHECK_EQ(secondSocket, std::string(kTestSocketDir) + "/wayland-1");
    CHECK(pathExists(secondSocket));
    CHECK(pathExists(firstSocket));
}

TEST_CASE(wayland_socket_explicit_name_and_path) {
    using namespace Wayland::Compositor;
    SocketEnvironmentGuard guard;
    resetSocketDir();
    ::setenv("FIRMIR_WAYLAND_SOCKET_DIR", kTestSocketDir, 1);

    ::setenv("FIRMIR_WAYLAND_SOCKET", "custom.sock", 1);
    {
        WaylandCompositor compositor;
        CHECK(compositor.create());
        // A plain name lives in the socket directory, no wayland-N scan.
        CHECK_EQ(compositor.addSocketAuto(), std::string(kTestSocketDir) + "/custom.sock");
    }

    ::setenv("FIRMIR_WAYLAND_SOCKET", (std::string(kTestSocketDir) + "/absolute.sock").c_str(), 1);
    {
        WaylandCompositor compositor;
        CHECK(compositor.create());
        // An absolute path is used verbatim.
        CHECK_EQ(compositor.addSocketAuto(), std::string(kTestSocketDir) + "/absolute.sock");
    }
}

// WaylandKeyboard::setDefaultKeymap(): the compositor library owns the
// xkbcommon code, so a compositor needs no xkb dependency of its own.
TEST_CASE(wayland_seat_default_keymap) {
    using namespace Wayland::Compositor;
    WaylandCompositor compositor;
    CHECK(compositor.create());
    auto* seat = new WaylandSeat(&compositor);
    seat->setCapabilities(WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    CHECK(seat->initialize(&compositor));
    CHECK(seat->keyboard() != nullptr);
    CHECK(seat->keyboard()->setDefaultKeymap());
}

// WaylandGuiItem: the item owns the pointer/keyboard forwarding, so an
// embedding compositor creates a plain WaylandGuiItem.
TEST_CASE(wayland_gui_item_input_state) {
    using namespace Wayland::Compositor;
    WaylandGuiItem item;
    // A hosted surface is a window: clicks give it the keyboard focus.
    CHECK(item.focusPolicy() == Gui::FocusPolicy::ClickFocus);
    CHECK(item.seat() == nullptr);

    WaylandSeat seat;
    item.setSeat(&seat);
    CHECK(item.seat() == &seat);

    WaylandSurface surface;
    WaylandView view;
    view.setSurface(&surface);
    item.setView(&view);
    CHECK(item.view() == &view);
    CHECK_EQ(view.surface(), &surface);
    // Detaching (surface destroyed by its client) stops the painting.
    item.detachSurface();
    CHECK_EQ(view.surface(), nullptr);
}


// WaylandBufferRef plus the compositor's buffer backend subsystem: the plugin
// list is what selects drm_egl over shm, so pin its order and the file names.
TEST_CASE(wayland_compositor_buffer_backend_selection) {
    using namespace Wayland::Compositor;
    ::unsetenv("FIRMIR_WAYLAND_COMPOSITOR_BACKENDS");
    ::unsetenv("FIRMIR_WAYLAND_COMPOSITOR_BACKEND");
    const std::vector<std::string> defaults = BufferBackends::candidates();
    CHECK_EQ(defaults.size(), std::size_t(2));
    CHECK_EQ(defaults[0], std::string("drm_egl")); // accelerated first
    CHECK_EQ(defaults[1], std::string("shm"));

    ::setenv("FIRMIR_WAYLAND_COMPOSITOR_BACKENDS", " shm , drm_egl ", 1);
    const std::vector<std::string> listed = BufferBackends::candidates();
    CHECK_EQ(listed.size(), std::size_t(2));
    CHECK_EQ(listed[0], std::string("shm"));
    CHECK_EQ(listed[1], std::string("drm_egl"));

    ::setenv("FIRMIR_WAYLAND_COMPOSITOR_BACKEND", "shm", 1);
    const std::vector<std::string> pinned = BufferBackends::candidates();
    CHECK_EQ(pinned.size(), std::size_t(1));
    CHECK_EQ(pinned[0], std::string("shm"));

#ifdef _WIN32
    const std::string suffix = ".dll";
#else
    const std::string suffix = ".so";
#endif
    CHECK_EQ(BufferBackends::pluginFileName("drm_egl"),
             std::string("firmir-wayland-compositor-drm-egl") + suffix);
    CHECK_EQ(BufferBackends::pluginFileName("shm"),
             std::string("firmir-wayland-compositor-shm") + suffix);

    ::unsetenv("FIRMIR_WAYLAND_COMPOSITOR_BACKEND");
    ::unsetenv("FIRMIR_WAYLAND_COMPOSITOR_BACKENDS");
    ::setenv("FIRMIR_WAYLAND_COMPOSITOR_PLUGIN_PATH", "/opt/firmir/wl", 1);
    bool found = false;
    for (const std::string& dir : BufferBackends::searchDirs())
        if (dir == "/opt/firmir/wl")
            found = true;
    CHECK(found);
    ::unsetenv("FIRMIR_WAYLAND_COMPOSITOR_PLUGIN_PATH");
}

// The client side of the same subsystem: drm_egl is probed first, shm is the
// fallback, and the pin/list environment variables reorder them.
TEST_CASE(wayland_client_buffer_backend_selection) {
    using namespace Wayland::Client;
    ::unsetenv("FIRMIR_WAYLAND_CLIENT_BACKENDS");
    ::unsetenv("FIRMIR_WAYLAND_CLIENT_BACKEND");
    const std::vector<std::string> defaults = Backends::candidates();
    CHECK_EQ(defaults.size(), std::size_t(2));
    CHECK_EQ(defaults[0], std::string("drm_egl")); // accelerated first
    CHECK_EQ(defaults[1], std::string("shm"));

    ::setenv("FIRMIR_WAYLAND_CLIENT_BACKENDS", "shm,drm_egl", 1);
    const std::vector<std::string> listed = Backends::candidates();
    CHECK_EQ(listed.size(), std::size_t(2));
    CHECK_EQ(listed[0], std::string("shm"));
    CHECK_EQ(listed[1], std::string("drm_egl"));

    ::setenv("FIRMIR_WAYLAND_CLIENT_BACKEND", "shm", 1);
    const std::vector<std::string> pinned = Backends::candidates();
    CHECK_EQ(pinned.size(), std::size_t(1));
    CHECK_EQ(pinned[0], std::string("shm"));

#ifdef _WIN32
    const std::string suffix = ".dll";
#else
    const std::string suffix = ".so";
#endif
    CHECK_EQ(Backends::pluginFileName("drm_egl"),
             std::string("firmir-wayland-client-drm-egl") + suffix);

    ::unsetenv("FIRMIR_WAYLAND_CLIENT_BACKEND");
    ::unsetenv("FIRMIR_WAYLAND_CLIENT_BACKENDS");
    ::setenv("FIRMIR_WAYLAND_CLIENT_PLUGIN_PATH", "/opt/firmir/wl", 1);
    bool found = false;
    for (const std::string& dir : Backends::searchDirs())
        if (dir == "/opt/firmir/wl")
            found = true;
    CHECK(found);
    ::unsetenv("FIRMIR_WAYLAND_CLIENT_PLUGIN_PATH");
}

// A client that disconnects must leave clients() and lose its surfaces: a
// compositor that hosts apps which come and go otherwise kept a WaylandClient —
// and every resource it had bound — for each connection it had ever served, so a
// long-running session grew with every app that was opened and closed.
//
// Everything runs on one thread: the compositor's objects are owned by the
// thread that created them, and dispatch is called in between client requests
// instead of from a server thread.
TEST_CASE(wayland_disconnected_client_is_forgotten) {
    using namespace Wayland::Compositor;
    SocketEnvironmentGuard guard;
    resetSocketDir();
    ::setenv("FIRMIR_WAYLAND_SOCKET_DIR", kTestSocketDir, 1);

    WaylandCompositor compositor;
    CHECK(compositor.create());
    auto* legacy = new WaylandWlShell(&compositor);
    CHECK(legacy->initialize(&compositor));
    const std::string socket = compositor.addSocketAuto();
    CHECK(!socket.empty());
    if (socket.empty())
        return;
    CHECK(compositor.clients().empty());
    CHECK(compositor.surfaces().empty());

    // Sends what the client queued so far to the server and processes it (plus
    // whatever the server replied) so the next step starts from a clean state.
    // Several rounds: the first one accepts a new connection, the next ones read
    // what that client already sent.
    const auto flushToServer = [&] {
        for (int round = 0; round < 4; ++round)
            compositor.dispatch(5);
        if (compositor.display())
            compositor.display()->flushClients();
    };

    {
        Wayland::Client::Display display(socket.c_str());
        CHECK(display.valid());
        if (!display.valid())
            return;
        auto registry = display.createRegistry();
        std::uint32_t compositorName = 0;
        std::uint32_t shellName = 0;
        registry->setGlobalHandler([&](std::uint32_t name, const char* interface,
                                       std::uint32_t version) {
            (void)version;
            if (std::strcmp(interface, wl_compositor_interface.name) == 0)
                compositorName = name;
            else if (std::strcmp(interface, wl_shell_interface.name) == 0)
                shellName = name;
        });
        // get_registry -> globals: the reply is queued before the client reads
        // it, so the client-side dispatch below cannot block.
        display.flush();
        flushToServer();
        display.dispatch();
        CHECK(compositorName != 0);
        CHECK(shellName != 0);
        if (!compositorName || !shellName)
            return;

        auto wlCompositor = Wayland::Client::Compositor::bind(*registry, compositorName, 4);
        auto shell = Wayland::Client::Shell::bind(*registry, shellName, 1);
        auto surface = wlCompositor.createSurface();
        auto shellSurface = shell.getShellSurface(surface.handle());
        shellSurface.setToplevel();
        display.flush();
        flushToServer();

        CHECK_EQ(compositor.clients().size(), std::size_t(1));
        CHECK_EQ(compositor.surfaces().size(), std::size_t(1));
        CHECK_EQ(legacy->shellSurfaces().size(), std::size_t(1));
    } // the client connection ends here

    // The disconnect is queued in the server's socket: process it and the client
    // and its surface must be gone.
    for (int attempt = 0; attempt < 100; ++attempt) {
        compositor.dispatch(10);
        if (compositor.clients().empty() && compositor.surfaces().empty())
            break;
    }
    CHECK(compositor.clients().empty());
    CHECK(compositor.surfaces().empty());
    CHECK(legacy->shellSurfaces().empty());
}

// End to end: the loader dlopen()s the shm client backend, the plugin allocates
// a wl_shm buffer over a real compositor connection, and the pixels it hands
// out are the compositor's frame memory.
TEST_CASE(wayland_client_shm_backend_buffers) {
#ifdef FIRMIR_WAYLAND_TEST_CLIENT_PLUGIN_DIR
    using namespace Wayland::Client;
    ::setenv("FIRMIR_WAYLAND_CLIENT_PLUGIN_PATH",
             FIRMIR_WAYLAND_TEST_CLIENT_PLUGIN_DIR, 1);
    ::setenv("FIRMIR_WAYLAND_CLIENT_BACKEND", "shm", 1);
    ::setenv("FIRMIR_WAYLAND_SOCKET_DIR", kTestSocketDir, 1);
    resetSocketDir();

    Wayland::Compositor::WaylandCompositor compositor;
    CHECK(compositor.create());
    const std::string socket = compositor.addSocketAuto();
    CHECK(!socket.empty());
    if (socket.empty()) {
        ::unsetenv("FIRMIR_WAYLAND_CLIENT_PLUGIN_PATH");
        ::unsetenv("FIRMIR_WAYLAND_CLIENT_BACKEND");
        ::unsetenv("FIRMIR_WAYLAND_SOCKET_DIR");
        return;
    }
    // Serve the connection from a thread: a client roundtrip needs the
    // compositor to dispatch while the test thread waits.
    std::atomic<bool> serving{true};
    std::thread server([&] {
        while (serving.load())
            compositor.dispatch(10);
    });

    {
        Wayland::Client::Display display(socket.c_str());
        CHECK(display.valid());
        if (display.valid()) {
            auto registry = display.createRegistry();
            wl_shm* shm = nullptr;
            registry->setGlobalHandler(
                [&](std::uint32_t name, const char* interface,
                    std::uint32_t version) {
                    (void)version;
                    if (std::strcmp(interface, wl_shm_interface.name) == 0 && !shm)
                        shm = static_cast<wl_shm*>(wl_registry_bind(
                            registry->handle(), name, &wl_shm_interface, 1));
                });
            display.roundtrip();
            CHECK(shm != nullptr);
            if (shm) {
                BackendParams params;
                params.display = display.handle();
                params.registry = registry->handle();
                params.shm = shm;
                std::unique_ptr<Backend> backend = Backends::create(params);
                CHECK(backend != nullptr);
                if (backend) {
                    CHECK_EQ(std::string(backend->name()), std::string("shm"));
                    std::unique_ptr<BackendBuffer> buffer =
                        backend->createBuffer(32, 16);
                    CHECK(buffer != nullptr);
                    if (buffer) {
                        CHECK_EQ(buffer->width(), 32);
                        CHECK_EQ(buffer->height(), 16);
                        CHECK_EQ(buffer->stride(), 32 * 4);
                        CHECK_EQ(buffer->bytes(), std::size_t(32) * 16 * 4);
                        CHECK(buffer->handle() != nullptr);
                        CHECK(buffer->pixels() != nullptr);
                        if (buffer->pixels()) {
                            buffer->pixels()[0] = 0xff00ff00u;
                            CHECK_EQ(buffer->pixels()[0], 0xff00ff00u);
                        }
                        CHECK(buffer->commitPixels());
                    }
                }
                wl_shm_destroy(shm);
            }
        }
    }

    serving = false;
    server.join();
    ::unsetenv("FIRMIR_WAYLAND_CLIENT_PLUGIN_PATH");
    ::unsetenv("FIRMIR_WAYLAND_CLIENT_BACKEND");
    ::unsetenv("FIRMIR_WAYLAND_SOCKET_DIR");
#endif
}


