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

class WaylandKeyboard::Impl {
public:
    WaylandResource resource;
};

class WaylandPointer::Impl {
public:
    WaylandResource resource;
};

class WaylandTouch::Impl {
public:
    WaylandResource resource;
};

class WaylandSeat::Impl {
public:
    std::uint32_t capabilities = 0;
    wl_global* global = nullptr;
    std::unique_ptr<WaylandKeyboard> keyboard;
    std::unique_ptr<WaylandPointer> pointer;
    std::unique_ptr<WaylandTouch> touch;
};

WaylandSeat::WaylandSeat(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {
    d->keyboard = std::make_unique<WaylandKeyboard>(this);
    d->pointer = std::make_unique<WaylandPointer>(this);
    d->touch = std::make_unique<WaylandTouch>(this);
}
WaylandSeat::~WaylandSeat() { if (d->global) wl_global_destroy(d->global); }
std::uint32_t WaylandSeat::capabilities() const { return d->capabilities; }
WaylandKeyboard* WaylandSeat::keyboard() const { return d->keyboard.get(); }
WaylandPointer* WaylandSeat::pointer() const { return d->pointer.get(); }
WaylandTouch* WaylandSeat::touch() const { return d->touch.get(); }
bool WaylandSeat::initialize(WaylandCompositor* compositor) {
    if (!compositor || !compositor->display()) return false;
    d->global = compositor->display()->createGlobal(&wl_seat_interface, 7, bindSeat, this);
    return d->global != nullptr;
}
void WaylandSeat::setCapabilities(std::uint32_t capabilities) { d->capabilities = capabilities; }

WaylandKeyboard::WaylandKeyboard(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
WaylandKeyboard::~WaylandKeyboard() = default;
WaylandResource WaylandKeyboard::resource() const { return d->resource; }
void WaylandKeyboard::setResource(wl_resource* resource) { d->resource = WaylandResource(resource); }

WaylandPointer::WaylandPointer(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
WaylandPointer::~WaylandPointer() = default;
WaylandResource WaylandPointer::resource() const { return d->resource; }
void WaylandPointer::setResource(wl_resource* resource) { d->resource = WaylandResource(resource); }

WaylandTouch::WaylandTouch(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
WaylandTouch::~WaylandTouch() = default;
WaylandResource WaylandTouch::resource() const { return d->resource; }
void WaylandTouch::setResource(wl_resource* resource) { d->resource = WaylandResource(resource); }

void WaylandKeyboard::setKeymap(int fd, std::uint32_t size) {
    if (d->resource.handle())
        wl_keyboard_send_keymap(d->resource.handle(), WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
}
void WaylandKeyboard::enter(WaylandSurface* surface, const std::vector<std::uint32_t>& keys) {
    if (!d->resource.handle() || !surface || !surface->resource().handle()) return;
    wl_array array{};
    array.data = const_cast<std::uint32_t*>(keys.data());
    array.size = array.alloc = keys.size() * sizeof(std::uint32_t);
    wl_keyboard_send_enter(d->resource.handle(), serial(d->resource.handle()),
                           surface->resource().handle(), &array);
}
void WaylandKeyboard::leave(WaylandSurface* surface) {
    if (d->resource.handle() && surface)
        wl_keyboard_send_leave(d->resource.handle(), serial(d->resource.handle()),
                               surface->resource().handle());
}
void WaylandKeyboard::key(std::uint32_t time, std::uint32_t keyCode, bool pressed) {
    if (d->resource.handle())
        wl_keyboard_send_key(d->resource.handle(), serial(d->resource.handle()), time, keyCode,
                             pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
}
void WaylandKeyboard::modifiers(std::uint32_t depressed, std::uint32_t latched,
                                std::uint32_t locked, std::uint32_t group) {
    if (d->resource.handle())
        wl_keyboard_send_modifiers(d->resource.handle(), serial(d->resource.handle()),
                                   depressed, latched, locked, group);
}

void WaylandPointer::enter(WaylandSurface* surface, float x, float y) {
    if (d->resource.handle() && surface)
        wl_pointer_send_enter(d->resource.handle(), serial(d->resource.handle()),
            surface->resource().handle(),
            wl_fixed_from_double(x), wl_fixed_from_double(y));
}
void WaylandPointer::leave(WaylandSurface* surface) {
    if (d->resource.handle() && surface)
        wl_pointer_send_leave(d->resource.handle(), serial(d->resource.handle()),
                              surface->resource().handle());
}
void WaylandPointer::motion(std::uint32_t time, float x, float y) {
    if (d->resource.handle())
        wl_pointer_send_motion(d->resource.handle(), time, wl_fixed_from_double(x), wl_fixed_from_double(y));
}
void WaylandPointer::button(std::uint32_t time, std::uint32_t buttonCode, bool pressed) {
    if (d->resource.handle())
        wl_pointer_send_button(d->resource.handle(), serial(d->resource.handle()), time, buttonCode,
            pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
}
void WaylandPointer::axis(std::uint32_t time, std::uint32_t axisValue, float value) {
    if (d->resource.handle())
        wl_pointer_send_axis(d->resource.handle(), time, axisValue, wl_fixed_from_double(value));
}

void WaylandTouch::down(WaylandSurface* surface, std::uint32_t time, int id, float x, float y) {
    if (d->resource.handle() && surface)
        wl_touch_send_down(d->resource.handle(), serial(d->resource.handle()), time,
            surface->resource().handle(), id,
            wl_fixed_from_double(x), wl_fixed_from_double(y));
}
void WaylandTouch::up(std::uint32_t time, int id) {
    if (d->resource.handle()) wl_touch_send_up(d->resource.handle(), serial(d->resource.handle()), time, id);
}
void WaylandTouch::motion(std::uint32_t time, int id, float x, float y) {
    if (d->resource.handle())
        wl_touch_send_motion(d->resource.handle(), time, id, wl_fixed_from_double(x), wl_fixed_from_double(y));
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
