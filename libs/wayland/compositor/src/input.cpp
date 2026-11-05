#include <firmir/wayland/compositor/input.hpp>
#include <wayland-server.h>
#include <algorithm>

namespace Firmir {
namespace Wayland {
namespace Compositor {
namespace {

std::uint32_t serial(wl_resource* resource) {
    if (!resource) return 0;
    return wl_display_next_serial(wl_client_get_display(wl_resource_get_client(resource)));
}
void releaseResource(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
void setCursor(wl_client*, wl_resource*, std::uint32_t, wl_resource*, std::int32_t, std::int32_t) {}
const struct wl_pointer_interface pointerImplementation = {setCursor, releaseResource};
const struct wl_keyboard_interface keyboardImplementation = {releaseResource};
const struct wl_touch_interface touchImplementation = {releaseResource};

void bindSeat(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

void getPointer(wl_client* client, wl_resource* seatResource, std::uint32_t id) {
    auto* seat = static_cast<WaylandSeat*>(wl_resource_get_user_data(seatResource));
    wl_resource* resource = wl_resource_create(client, &wl_pointer_interface, 7, id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &pointerImplementation, seat, nullptr);
    seat->pointer()->setResource(resource);
}
void getKeyboard(wl_client* client, wl_resource* seatResource, std::uint32_t id) {
    auto* seat = static_cast<WaylandSeat*>(wl_resource_get_user_data(seatResource));
    wl_resource* resource = wl_resource_create(client, &wl_keyboard_interface, 7, id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &keyboardImplementation, seat, nullptr);
    seat->keyboard()->setResource(resource);
}
void getTouch(wl_client* client, wl_resource* seatResource, std::uint32_t id) {
    auto* seat = static_cast<WaylandSeat*>(wl_resource_get_user_data(seatResource));
    wl_resource* resource = wl_resource_create(client, &wl_touch_interface, 7, id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &touchImplementation, seat, nullptr);
    seat->touch()->setResource(resource);
}
const struct wl_seat_interface seatImplementation = {getPointer, getKeyboard, getTouch, releaseResource};

void bindSeat(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
    auto* seat = static_cast<WaylandSeat*>(data);
    const int bounded = std::min(7u, version);
    wl_resource* resource = wl_resource_create(client, &wl_seat_interface, bounded, id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &seatImplementation, seat, nullptr);
    if (seat->capabilities()) wl_seat_send_capabilities(resource, seat->capabilities());
    if (bounded >= WL_SEAT_NAME_SINCE_VERSION) wl_seat_send_name(resource, "firmir-seat0");
}

} // namespace

WaylandSeat::WaylandSeat(Object* parent)
    : WaylandObject(parent), m_keyboard(std::make_unique<WaylandKeyboard>(this)),
      m_pointer(std::make_unique<WaylandPointer>(this)), m_touch(std::make_unique<WaylandTouch>(this)) {}
WaylandSeat::~WaylandSeat() { if (m_global) wl_global_destroy(m_global); }
bool WaylandSeat::initialize(WaylandCompositor* compositor) {
    if (!compositor || !compositor->display()) return false;
    m_global = compositor->display()->createGlobal(&wl_seat_interface, 7, bindSeat, this);
    return m_global != nullptr;
}
void WaylandSeat::setCapabilities(std::uint32_t capabilities) { m_capabilities = capabilities; }

void WaylandKeyboard::setKeymap(int fd, std::uint32_t size) {
    if (m_resource.handle())
        wl_keyboard_send_keymap(m_resource.handle(), WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
}
void WaylandKeyboard::enter(WaylandSurface* surface, const std::vector<std::uint32_t>& keys) {
    if (!m_resource.handle() || !surface || !surface->resource().handle()) return;
    wl_array array{};
    array.data = const_cast<std::uint32_t*>(keys.data());
    array.size = array.alloc = keys.size() * sizeof(std::uint32_t);
    wl_keyboard_send_enter(m_resource.handle(), serial(m_resource.handle()),
                           surface->resource().handle(), &array);
}
void WaylandKeyboard::leave(WaylandSurface* surface) {
    if (m_resource.handle() && surface)
        wl_keyboard_send_leave(m_resource.handle(), serial(m_resource.handle()),
                               surface->resource().handle());
}
void WaylandKeyboard::key(std::uint32_t time, std::uint32_t keyCode, bool pressed) {
    if (m_resource.handle())
        wl_keyboard_send_key(m_resource.handle(), serial(m_resource.handle()), time, keyCode,
                             pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
}
void WaylandKeyboard::modifiers(std::uint32_t depressed, std::uint32_t latched,
                                std::uint32_t locked, std::uint32_t group) {
    if (m_resource.handle())
        wl_keyboard_send_modifiers(m_resource.handle(), serial(m_resource.handle()),
                                   depressed, latched, locked, group);
}

void WaylandPointer::enter(WaylandSurface* surface, float x, float y) {
    if (m_resource.handle() && surface)
        wl_pointer_send_enter(m_resource.handle(), serial(m_resource.handle()),
            surface->resource().handle(),
            wl_fixed_from_double(x), wl_fixed_from_double(y));
}
void WaylandPointer::leave(WaylandSurface* surface) {
    if (m_resource.handle() && surface)
        wl_pointer_send_leave(m_resource.handle(), serial(m_resource.handle()),
                              surface->resource().handle());
}
void WaylandPointer::motion(std::uint32_t time, float x, float y) {
    if (m_resource.handle())
        wl_pointer_send_motion(m_resource.handle(), time, wl_fixed_from_double(x), wl_fixed_from_double(y));
}
void WaylandPointer::button(std::uint32_t time, std::uint32_t buttonCode, bool pressed) {
    if (m_resource.handle())
        wl_pointer_send_button(m_resource.handle(), serial(m_resource.handle()), time, buttonCode,
            pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
}
void WaylandPointer::axis(std::uint32_t time, std::uint32_t axisValue, float value) {
    if (m_resource.handle())
        wl_pointer_send_axis(m_resource.handle(), time, axisValue, wl_fixed_from_double(value));
}

void WaylandTouch::down(WaylandSurface* surface, std::uint32_t time, int id, float x, float y) {
    if (m_resource.handle() && surface)
        wl_touch_send_down(m_resource.handle(), serial(m_resource.handle()), time,
            surface->resource().handle(), id,
            wl_fixed_from_double(x), wl_fixed_from_double(y));
}
void WaylandTouch::up(std::uint32_t time, int id) {
    if (m_resource.handle()) wl_touch_send_up(m_resource.handle(), serial(m_resource.handle()), time, id);
}
void WaylandTouch::motion(std::uint32_t time, int id, float x, float y) {
    if (m_resource.handle())
        wl_touch_send_motion(m_resource.handle(), time, id, wl_fixed_from_double(x), wl_fixed_from_double(y));
}
void WaylandTextInputManager::commitString(WaylandSurface* surface, const std::string& text) {
    if (surface) surface->emitSignal("textInput", text);
}
void WaylandTextInputManager::setPreeditString(WaylandSurface* surface, const std::string& text) {
    if (surface) surface->emitSignal("preeditString", text);
}
} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
