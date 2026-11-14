#include "test_framework.hpp"

#include <core_client.hpp>
#include <xdg_shell_client.hpp>
#include <firmir/wayland/compositor/compositor.hpp>

#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

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
