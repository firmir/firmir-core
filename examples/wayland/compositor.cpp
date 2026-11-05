#include <firmir/wayland/compositor/compositor.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace Firmir::Wayland::Compositor;

int main() {
    WaylandCompositor compositor;
    if (!compositor.create()) {
        std::fprintf(stderr, "example-compositor: create failed\n");
        return 1;
    }
    compositor.addOutput({1920, 1080});
    auto* xdg = new WaylandXdgShell(&compositor);
    xdg->initialize(&compositor);
    auto* legacy = new WaylandWlShell(&compositor);
    legacy->initialize(&compositor);
    auto* seat = new WaylandSeat(&compositor);
    seat->setCapabilities(WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD |
                          WL_SEAT_CAPABILITY_TOUCH);
    seat->initialize(&compositor);
    auto* viewporter = new WaylandViewporter(&compositor);
    viewporter->initialize(&compositor);
    auto* decoration = new WaylandXdgDecorationManagerV1(&compositor);
    decoration->setDefaultMode(WaylandXdgDecorationManagerV1::Mode::ServerSide);
    decoration->initialize(&compositor);
    auto* outputs = new WaylandXdgOutputManagerV1(&compositor);
    outputs->initialize(&compositor);
    const std::string socket = compositor.addSocketAuto();
    if (socket.empty()) {
        std::fprintf(stderr, "example-compositor: add_socket failed\n");
        return 1;
    }
    std::printf("WAYLAND_DISPLAY=%s\n", socket.c_str());
    std::fflush(stdout);
    const char* timeout = std::getenv("FIRMIR_COMPOSITOR_TIMEOUT");
    if (!timeout)
        return compositor.exec();
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(std::atoi(timeout));
    while (std::chrono::steady_clock::now() < deadline)
        compositor.dispatch(100);
    return 0;
}
