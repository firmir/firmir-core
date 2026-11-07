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

Display::Display(const char* name) : m_display(wl_display_connect(name)) {
    trace("connect name=%s -> %p (errno=%d)", name ? name : "(default)",
          static_cast<void*>(m_display), m_display ? 0 : errno);
}
Display::~Display() { if (m_display) wl_display_disconnect(m_display); }
int Display::fd() const { return m_display ? wl_display_get_fd(m_display) : -1; }
int Display::dispatch() {
    if (!m_display)
        return -1;
    const int n = wl_display_dispatch(m_display);
    if (n < 0)
        trace("dispatch error=%d errno=%d", n, errno);
    return n;
}
int Display::roundtrip() {
    if (!m_display)
        return -1;
    const int n = wl_display_roundtrip(m_display);
    trace("roundtrip -> %d (protocol_error=%d)", n,
          m_display ? wl_display_get_error(m_display) : 0);
    return n;
}
int Display::flush() {
    if (!m_display)
        return -1;
    const int n = wl_display_flush(m_display);
    if (n < 0 && errno != EAGAIN)
        trace("flush error=%d errno=%d", n, errno);
    return n;
}
int Display::prepareRead() { return m_display ? wl_display_prepare_read(m_display) : -1; }
void Display::cancelRead() { if (m_display) wl_display_cancel_read(m_display); }
int Display::readEvents() {
    if (!m_display)
        return -1;
    const int n = wl_display_read_events(m_display);
    if (n < 0)
        trace("readEvents error=%d errno=%d", n, errno);
    return n;
}
int Display::dispatchPending() {
    if (!m_display)
        return -1;
    const int n = wl_display_dispatch_pending(m_display);
    if (n < 0)
        trace("dispatchPending error=%d errno=%d", n, errno);
    return n;
}
std::unique_ptr<Registry> Display::createRegistry() {
    if (!m_display) return nullptr;
    wl_registry* registry = wl_display_get_registry(m_display);
    trace("get_registry -> %p", static_cast<void*>(registry));
    return registry ? std::make_unique<Registry>(registry) : nullptr;
}

Registry::Registry(wl_registry* registry) : m_registry(registry) {
    if (m_registry) wl_registry_add_listener(m_registry, &s_listener, this);
}
Registry::~Registry() { if (m_registry) wl_registry_destroy(m_registry); }
void Registry::onGlobal(void* data, wl_registry*, std::uint32_t name,
                        const char* interface, std::uint32_t version) {
    auto* self = static_cast<Registry*>(data);
    if (self->m_global) self->m_global(name, interface, version);
}
void Registry::onGlobalRemove(void* data, wl_registry*, std::uint32_t name) {
    auto* self = static_cast<Registry*>(data);
    if (self->m_remove) self->m_remove(name);
}

} // namespace Client
} // namespace Wayland
} // namespace Firmir
