#include <firmir/wayland/client/display.hpp>
#include <firmir/wayland/client/registry.hpp>
#include <wayland-client.h>

namespace Firmir {
namespace Wayland {
namespace Client {

Display::Display(const char* name) : m_display(wl_display_connect(name)) {}
Display::~Display() { if (m_display) wl_display_disconnect(m_display); }
int Display::fd() const { return m_display ? wl_display_get_fd(m_display) : -1; }
int Display::dispatch() { return m_display ? wl_display_dispatch(m_display) : -1; }
int Display::roundtrip() { return m_display ? wl_display_roundtrip(m_display) : -1; }
int Display::flush() { return m_display ? wl_display_flush(m_display) : -1; }
int Display::prepareRead() { return m_display ? wl_display_prepare_read(m_display) : -1; }
void Display::cancelRead() { if (m_display) wl_display_cancel_read(m_display); }
int Display::readEvents() { return m_display ? wl_display_read_events(m_display) : -1; }
int Display::dispatchPending() { return m_display ? wl_display_dispatch_pending(m_display) : -1; }
std::unique_ptr<Registry> Display::createRegistry() {
    if (!m_display) return nullptr;
    wl_registry* registry = wl_display_get_registry(m_display);
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
