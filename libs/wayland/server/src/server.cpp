#include <firmir/wayland/server/server.hpp>
#include <wayland-server.h>

namespace Firmir {
namespace Wayland {
namespace Server {

Display::Display() : m_display(wl_display_create()) {}
Display::~Display() { if (m_display) wl_display_destroy(m_display); }
wl_event_loop* Display::eventLoop() const {
    return m_display ? wl_display_get_event_loop(m_display) : nullptr;
}
std::string Display::addSocketAuto() {
    const char* name = m_display ? wl_display_add_socket_auto(m_display) : nullptr;
    return name ? name : "";
}
int Display::run() { if (!m_display) return -1; wl_display_run(m_display); return 0; }
void Display::terminate() { if (m_display) wl_display_terminate(m_display); }
void Display::flushClients() { if (m_display) wl_display_flush_clients(m_display); }
int Display::dispatch(int timeoutMs) {
    return m_display ? wl_event_loop_dispatch(eventLoop(), timeoutMs) : -1;
}
std::uint32_t Display::nextSerial() { return m_display ? wl_display_next_serial(m_display) : 0; }
wl_global* Display::createGlobal(const wl_interface* interface, int version,
                                 wl_global_bind_func_t bind, void* data) {
    return m_display ? wl_global_create(m_display, interface, version, data, bind) : nullptr;
}
wl_display* Client::display() const { return m_client ? wl_client_get_display(m_client) : nullptr; }
void Client::credentials(pid_t& pid, uid_t& uid, gid_t& gid) const {
    pid = uid = gid = 0;
    if (m_client) wl_client_get_credentials(m_client, &pid, &uid, &gid);
}
void Client::flush() const { if (m_client) wl_client_flush(m_client); }

} // namespace Server
} // namespace Wayland
} // namespace Firmir
