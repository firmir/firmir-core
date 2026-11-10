#include <firmir/wayland/client/display.hpp>
#include <firmir/wayland/client/registry.hpp>
#include <wayland-client.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace Firmir {
namespace Wayland {
namespace Client {

namespace {

// Wire-level tracing of the client wrapper; enable with
// FIRMIR_WAYLAND_CLIENT_DEBUG=1. Logs connection setup, roundtrips and
// failing flush/read/dispatch calls so request/response correctness can be
// verified from the log.
bool clientTrace() {
    static const bool enabled =
        std::getenv("FIRMIR_WAYLAND_CLIENT_DEBUG") != nullptr;
    return enabled;
}

void trace(const char* fmt, ...) {
    if (!clientTrace())
        return;
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "firmir-wayland-client: ");
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}

} // namespace

class Display::Impl {
public:
    wl_display* display = nullptr;
};

class Registry::Impl {
public:
    wl_registry* registry = nullptr;
    Global global;
    GlobalRemove remove;
};

Display::Display(const char* name) : d(std::make_unique<Impl>()) {
    d->display = wl_display_connect(name);
    trace("connect name=%s -> %p (errno=%d)", name ? name : "(default)",
          static_cast<void*>(d->display), d->display ? 0 : errno);
}
Display::~Display() { if (d->display) wl_display_disconnect(d->display); }
bool Display::valid() const { return d->display != nullptr; }
wl_display* Display::handle() const { return d->display; }
int Display::fd() const { return d->display ? wl_display_get_fd(d->display) : -1; }
int Display::dispatch() {
    if (!d->display)
        return -1;
    const int n = wl_display_dispatch(d->display);
    if (n < 0)
        trace("dispatch error=%d errno=%d", n, errno);
    return n;
}
int Display::roundtrip() {
    if (!d->display)
        return -1;
    const int n = wl_display_roundtrip(d->display);
    trace("roundtrip -> %d (protocol_error=%d)", n,
          d->display ? wl_display_get_error(d->display) : 0);
    return n;
}
int Display::flush() {
    if (!d->display)
        return -1;
    const int n = wl_display_flush(d->display);
    if (n < 0 && errno != EAGAIN)
        trace("flush error=%d errno=%d", n, errno);
    return n;
}
int Display::prepareRead() { return d->display ? wl_display_prepare_read(d->display) : -1; }
void Display::cancelRead() { if (d->display) wl_display_cancel_read(d->display); }
int Display::readEvents() {
    if (!d->display)
        return -1;
    const int n = wl_display_read_events(d->display);
    if (n < 0)
        trace("readEvents error=%d errno=%d", n, errno);
    return n;
}
int Display::dispatchPending() {
    if (!d->display)
        return -1;
    const int n = wl_display_dispatch_pending(d->display);
    if (n < 0)
        trace("dispatchPending error=%d errno=%d", n, errno);
    return n;
}
int Display::lastError() const {
    return d->display ? wl_display_get_error(d->display) : ENOTCONN;
}
std::unique_ptr<Registry> Display::createRegistry() {
    if (!d->display) return nullptr;
    wl_registry* registry = wl_display_get_registry(d->display);
    trace("get_registry -> %p", static_cast<void*>(registry));
    return registry ? std::make_unique<Registry>(registry) : nullptr;
}

Registry::Registry(wl_registry* registry) : d(std::make_unique<Impl>()) {
    d->registry = registry;
    if (d->registry) wl_registry_add_listener(d->registry, &s_listener, this);
}
Registry::~Registry() { if (d->registry) wl_registry_destroy(d->registry); }
wl_registry* Registry::handle() const { return d->registry; }
void Registry::setGlobalHandler(Global handler) { d->global = std::move(handler); }
void Registry::setGlobalRemoveHandler(GlobalRemove handler) { d->remove = std::move(handler); }
void Registry::onGlobal(void* data, wl_registry*, std::uint32_t name,
                        const char* interface, std::uint32_t version) {
    auto* self = static_cast<Registry*>(data);
    if (self->d->global) self->d->global(name, interface, version);
}
void Registry::onGlobalRemove(void* data, wl_registry*, std::uint32_t name) {
    auto* self = static_cast<Registry*>(data);
    if (self->d->remove) self->d->remove(name);
}

} // namespace Client
} // namespace Wayland
} // namespace Firmir
