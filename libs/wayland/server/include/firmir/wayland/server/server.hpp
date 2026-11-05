#pragma once

#include <cstdint>
#include <string>
#include <wayland-server-core.h>

namespace Firmir {
namespace Wayland {
namespace Server {

class Display {
public:
    Display();
    ~Display();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;
    bool valid() const { return m_display != nullptr; }
    wl_display* handle() const { return m_display; }
    wl_event_loop* eventLoop() const;
    std::string addSocketAuto();
    int run();
    void terminate();
    void flushClients();
    int dispatch(int timeoutMs = 0);
    std::uint32_t nextSerial();
    wl_global* createGlobal(const wl_interface* interface, int version,
                            wl_global_bind_func_t bind, void* data);
private:
    wl_display* m_display;
};

class Client {
public:
    explicit Client(wl_client* client = nullptr) : m_client(client) {}
    wl_client* handle() const { return m_client; }
    wl_display* display() const;
    void credentials(pid_t& pid, uid_t& uid, gid_t& gid) const;
    void flush() const;
private:
    wl_client* m_client;
};

class Resource {
public:
    explicit Resource(wl_resource* resource = nullptr) : m_resource(resource) {}
    wl_resource* handle() const { return m_resource; }
    Client client() const { return Client(m_resource ? wl_resource_get_client(m_resource) : nullptr); }
    std::uint32_t id() const { return m_resource ? wl_resource_get_id(m_resource) : 0; }
    int version() const { return m_resource ? wl_resource_get_version(m_resource) : 0; }
    void* userData() const { return m_resource ? wl_resource_get_user_data(m_resource) : nullptr; }
    void setUserData(void* data) { if (m_resource) wl_resource_set_user_data(m_resource, data); }
    void postNoMemory() const { if (m_resource) wl_resource_post_no_memory(m_resource); }
private:
    wl_resource* m_resource;
};

} // namespace Server
} // namespace Wayland
} // namespace Firmir
