#include <firmir/wayland/server/server.hpp>
#include <wayland-server.h>

namespace Firmir {
namespace Wayland {
namespace Server {

class Display::Impl {
public:
    wl_display* display = nullptr;
};

Display::Display() : d(std::make_unique<Impl>()) {
    d->display = wl_display_create();
}
bool Display::valid() const { return d->display != nullptr; }
wl_display* Display::handle() const { return d->display; }
Display::~Display() { if (d->display) wl_display_destroy(d->display); }
wl_event_loop* Display::eventLoop() const {
    return d->display ? wl_display_get_event_loop(d->display) : nullptr;
}
std::string Display::addSocketAuto() {
    const char* name = d->display ? wl_display_add_socket_auto(d->display) : nullptr;
    return name ? name : "";
}
int Display::run() { if (!d->display) return -1; wl_display_run(d->display); return 0; }
void Display::terminate() { if (d->display) wl_display_terminate(d->display); }
void Display::flushClients() { if (d->display) wl_display_flush_clients(d->display); }
int Display::dispatch(int timeoutMs) {
    return d->display ? wl_event_loop_dispatch(eventLoop(), timeoutMs) : -1;
}
void Display::destroyClients() { if (d->display) wl_display_destroy_clients(d->display); }
std::uint32_t Display::nextSerial() { return d->display ? wl_display_next_serial(d->display) : 0; }
wl_global* Display::createGlobal(const wl_interface* interface, int version,
                                 wl_global_bind_func_t bind, void* data) {
    return d->display ? wl_global_create(d->display, interface, version, data, bind) : nullptr;
}
wl_display* Client::display() const { return d->client ? wl_client_get_display(d->client) : nullptr; }
void Client::credentials(pid_t& pid, uid_t& uid, gid_t& gid) const {
    pid = uid = gid = 0;
    if (d->client) wl_client_get_credentials(d->client, &pid, &uid, &gid);
}
void Client::flush() const { if (d->client) wl_client_flush(d->client); }

} // namespace Server
} // namespace Wayland
} // namespace Firmir
