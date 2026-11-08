#include "test_framework.hpp"

#include <core_client.hpp>
#include <xdg_shell_client.hpp>
#include <firmir/wayland/compositor/compositor.hpp>

using namespace Firmir;

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
