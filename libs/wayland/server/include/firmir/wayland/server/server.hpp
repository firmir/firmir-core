#pragma once

#include <cstdint>
#include <memory>
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
    bool valid() const;
    wl_display* handle() const;
    wl_event_loop* eventLoop() const;
    /// Creates the compositor socket: @c /run/display/wayland-0, then
    /// wayland-1 … wayland-31 while a live compositor holds the previous
    /// name (a stale socket file is replaced). The directory is created on
    /// demand — a bare KMS console has no XDG_RUNTIME_DIR.
    ///
    /// Environment overrides:
    ///  - @c FIRMIR_WAYLAND_SOCKET_DIR: socket directory (default
    ///    @c /run/display);
    ///  - @c FIRMIR_WAYLAND_SOCKET: exact socket name, or an absolute path
    ///    used verbatim (no automatic numbering; a busy name is an error,
    ///    not a fallback).
    ///
    /// @return the socket path (absolute when it does not live in
    /// XDG_RUNTIME_DIR, i.e. the value to put into WAYLAND_DISPLAY), or an
    /// empty string on failure.
    std::string addSocketAuto();
    int run();
    void terminate();
    void flushClients();
    int dispatch(int timeoutMs = 0);
    /// Disconnects all clients and destroys their resources (their destroy
    /// callbacks run while the caller's objects are still alive).
    void destroyClients();
    std::uint32_t nextSerial();
    wl_global* createGlobal(const wl_interface* interface, int version,
                            wl_global_bind_func_t bind, void* data);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

/// Copyable value wrapper around wl_client; the Impl pointer is shared
/// between copies (same pattern as Core::Json / Core::Log).
class Client {
public:
    explicit Client(wl_client* client = nullptr)
        : d(std::make_shared<Impl>()) {
        d->client = client;
    }
    wl_client* handle() const { return d->client; }
    wl_display* display() const;
    void credentials(pid_t& pid, uid_t& uid, gid_t& gid) const;
    void flush() const;
private:
    struct Impl {
        wl_client* client = nullptr;
    };
    std::shared_ptr<Impl> d;
};

/// Copyable value wrapper around wl_resource (see Client).
class Resource {
public:
    explicit Resource(wl_resource* resource = nullptr)
        : d(std::make_shared<Impl>()) {
        d->resource = resource;
    }
    wl_resource* handle() const { return d->resource; }
    Client client() const { return Client(d->resource ? wl_resource_get_client(d->resource) : nullptr); }
    std::uint32_t id() const { return d->resource ? wl_resource_get_id(d->resource) : 0; }
    int version() const { return d->resource ? wl_resource_get_version(d->resource) : 0; }
    void* userData() const { return d->resource ? wl_resource_get_user_data(d->resource) : nullptr; }
    void setUserData(void* data) { if (d->resource) wl_resource_set_user_data(d->resource, data); }
    void postNoMemory() const { if (d->resource) wl_resource_post_no_memory(d->resource); }
private:
    struct Impl {
        wl_resource* resource = nullptr;
    };
    std::shared_ptr<Impl> d;
};

} // namespace Server
} // namespace Wayland
} // namespace Firmir
