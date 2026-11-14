#include <firmir/wayland/compositor/input.hpp>
#include <wayland-server.h>

#include <xkbcommon/xkbcommon.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <unistd.h>

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

void pointerDestroyed(wl_resource* resource) {
    auto* seat = static_cast<WaylandSeat*>(wl_resource_get_user_data(resource));
    seat->pointer()->removeResource(resource);
}
void keyboardDestroyed(wl_resource* resource) {
    auto* seat = static_cast<WaylandSeat*>(wl_resource_get_user_data(resource));
    seat->keyboard()->removeResource(resource);
}
void touchDestroyed(wl_resource* resource) {
    auto* seat = static_cast<WaylandSeat*>(wl_resource_get_user_data(resource));
    seat->touch()->removeResource(resource);
}

void bindSeat(wl_client* client, void* data, std::uint32_t version, std::uint32_t id);

void getPointer(wl_client* client, wl_resource* seatResource, std::uint32_t id) {
    auto* seat = static_cast<WaylandSeat*>(wl_resource_get_user_data(seatResource));
    wl_resource* resource = wl_resource_create(client, &wl_pointer_interface,
                                  wl_resource_get_version(seatResource), id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &pointerImplementation, seat, pointerDestroyed);
    seat->pointer()->addResource(resource);
}
void getKeyboard(wl_client* client, wl_resource* seatResource, std::uint32_t id) {
    auto* seat = static_cast<WaylandSeat*>(wl_resource_get_user_data(seatResource));
    wl_resource* resource = wl_resource_create(client, &wl_keyboard_interface,
                                   wl_resource_get_version(seatResource), id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &keyboardImplementation, seat, keyboardDestroyed);
    seat->keyboard()->addResource(resource);
}
void getTouch(wl_client* client, wl_resource* seatResource, std::uint32_t id) {
    auto* seat = static_cast<WaylandSeat*>(wl_resource_get_user_data(seatResource));
    wl_resource* resource = wl_resource_create(client, &wl_touch_interface,
                                wl_resource_get_version(seatResource), id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &touchImplementation, seat, touchDestroyed);
    seat->touch()->addResource(resource);
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
    /// One resource per bound wl_keyboard; events are routed to the resource
    /// owned by the focused surface's client (multi-client safe).
    std::vector<WaylandResource> resources;
    /// Raw keymap bytes as passed to setKeymap(); replayed to later binds.
    std::string keymap;
    /// Repeat settings (wl_keyboard.repeat_info), replayed to later binds.
    std::int32_t repeatRate = 0;
    std::int32_t repeatDelay = 0;
    /// The client that currently holds keyboard focus (nullptr = none).
    wl_client* focusClient = nullptr;
};

class WaylandPointer::Impl {
public:
    std::vector<WaylandResource> resources;
    wl_client* focusClient = nullptr;
};

class WaylandTouch::Impl {
public:
    std::vector<WaylandResource> resources;
    wl_client* focusClient = nullptr;
};

namespace {

/// The resource bound by @p client out of @p resources, or nullptr.
inline wl_resource* resourceForClient(const std::vector<WaylandResource>& resources,
                                      wl_client* client) {
    if (!client) return nullptr;
    for (const WaylandResource& resource : resources)
        if (resource.client() == client) return resource.handle();
    return nullptr;
}

/// The resource bound by the client that owns @p surface, or nullptr.
inline wl_resource* resourceForSurface(const std::vector<WaylandResource>& resources,
                                       WaylandSurface* surface) {
    if (!surface || !surface->resource().handle()) return nullptr;
    return resourceForClient(resources, wl_resource_get_client(surface->resource().handle()));
}

} // namespace

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
WaylandResource WaylandKeyboard::resource() const {
    return d->resources.empty() ? WaylandResource() : d->resources.back();
}
void WaylandKeyboard::setResource(wl_resource* resource) {
    d->resources.clear();
    if (resource) d->resources.emplace_back(resource);
}
void WaylandKeyboard::addResource(wl_resource* resource) {
    if (!resource) return;
    d->resources.emplace_back(resource);
    if (wl_resource_get_version(resource) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
        wl_keyboard_send_repeat_info(resource, d->repeatRate, d->repeatDelay);
    if (d->keymap.empty()) return;
    // Replay the stored keymap to the freshly bound client.
    int fd = memfd_create("firmir-keymap", MFD_CLOEXEC);
    if (fd < 0) return;
    const char* data = d->keymap.data();
    std::size_t left = d->keymap.size();
    while (left > 0) {
        const ssize_t written = ::write(fd, data, left);
        if (written <= 0) { ::close(fd); return; }
        data += written;
        left -= static_cast<std::size_t>(written);
    }
    lseek(fd, 0, SEEK_SET);
    wl_keyboard_send_keymap(resource, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd,
                            static_cast<std::uint32_t>(d->keymap.size()));
    ::close(fd);
}
void WaylandKeyboard::setRepeatInfo(std::int32_t rate, std::int32_t delay) {
    d->repeatRate = rate;
    d->repeatDelay = delay;
    for (const WaylandResource& resource : d->resources)
        if (wl_resource_get_version(resource.handle()) >=
            WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
            wl_keyboard_send_repeat_info(resource.handle(), rate, delay);
}
void WaylandKeyboard::removeResource(wl_resource* resource) {
    auto& resources = d->resources;
    resources.erase(std::remove_if(resources.begin(), resources.end(),
        [resource](const WaylandResource& value) { return value.handle() == resource; }),
        resources.end());
}

WaylandPointer::WaylandPointer(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
WaylandPointer::~WaylandPointer() = default;
WaylandResource WaylandPointer::resource() const {
    return d->resources.empty() ? WaylandResource() : d->resources.back();
}
void WaylandPointer::setResource(wl_resource* resource) {
    d->resources.clear();
    if (resource) d->resources.emplace_back(resource);
}
void WaylandPointer::addResource(wl_resource* resource) {
    if (resource) d->resources.emplace_back(resource);
}
void WaylandPointer::removeResource(wl_resource* resource) {
    auto& resources = d->resources;
    resources.erase(std::remove_if(resources.begin(), resources.end(),
        [resource](const WaylandResource& value) { return value.handle() == resource; }),
        resources.end());
}

WaylandTouch::WaylandTouch(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
WaylandTouch::~WaylandTouch() = default;
WaylandResource WaylandTouch::resource() const {
    return d->resources.empty() ? WaylandResource() : d->resources.back();
}
void WaylandTouch::setResource(wl_resource* resource) {
    d->resources.clear();
    if (resource) d->resources.emplace_back(resource);
}
void WaylandTouch::addResource(wl_resource* resource) {
    if (resource) d->resources.emplace_back(resource);
}
void WaylandTouch::removeResource(wl_resource* resource) {
    auto& resources = d->resources;
    resources.erase(std::remove_if(resources.begin(), resources.end(),
        [resource](const WaylandResource& value) { return value.handle() == resource; }),
        resources.end());
}

void WaylandKeyboard::setKeymap(int fd, std::uint32_t size) {
    // Store the keymap bytes so later binds get the same map.
    d->keymap.assign(size, '\0');
    lseek(fd, 0, SEEK_SET);
    std::size_t offset = 0;
    while (offset < size) {
        const ssize_t got = ::read(fd, d->keymap.data() + offset, size - offset);
        if (got <= 0) { d->keymap.clear(); return; }
        offset += static_cast<std::size_t>(got);
    }
    for (const WaylandResource& resource : d->resources) {
        int copy = memfd_create("firmir-keymap", MFD_CLOEXEC);
        if (copy < 0) continue;
        const ssize_t written = ::write(copy, d->keymap.data(), d->keymap.size());
        if (written == static_cast<ssize_t>(d->keymap.size())) {
            lseek(copy, 0, SEEK_SET);
            wl_keyboard_send_keymap(resource.handle(), WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, copy,
                                    static_cast<std::uint32_t>(d->keymap.size()));
        }
        ::close(copy);
    }
}
bool WaylandKeyboard::setDefaultKeymap() {
    xkb_context* context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!context) return false;
    // No explicit rules: honour XKB_DEFAULT_RULES / MODEL / LAYOUT /
    // VARIANT / OPTIONS from the environment.
    xkb_keymap* keymap =
        xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    bool installed = false;
    if (keymap) {
        char* text = xkb_keymap_get_as_string(keymap, XKB_KEYMAP_FORMAT_TEXT_V1);
        if (text) {
            const std::size_t size = std::strlen(text) + 1;
            const int fd = memfd_create("firmir-keymap", MFD_CLOEXEC);
            if (fd >= 0) {
                std::size_t offset = 0;
                while (offset < size) {
                    const ssize_t chunk = ::write(fd, text + offset, size - offset);
                    if (chunk <= 0) break;
                    offset += static_cast<std::size_t>(chunk);
                }
                if (offset == size) {
                    setKeymap(fd, static_cast<std::uint32_t>(size));
                    installed = true;
                }
                ::close(fd);
            }
            std::free(text);
        }
        xkb_keymap_unref(keymap);
    }
    xkb_context_unref(context);
    if (installed) setRepeatInfo(25, 600);
    return installed;
}
void WaylandKeyboard::enter(WaylandSurface* surface, const std::vector<std::uint32_t>& keys) {
    wl_resource* target = resourceForSurface(d->resources, surface);
    if (!target) return;
    d->focusClient = wl_resource_get_client(target);
    wl_array array{};
    array.data = const_cast<std::uint32_t*>(keys.data());
    array.size = array.alloc = keys.size() * sizeof(std::uint32_t);
    wl_keyboard_send_enter(target, serial(target), surface->resource().handle(), &array);
}
void WaylandKeyboard::leave(WaylandSurface* surface) {
    wl_resource* target = resourceForSurface(d->resources, surface);
    if (!target) return;
    wl_keyboard_send_leave(target, serial(target), surface->resource().handle());
    if (d->focusClient == wl_resource_get_client(target)) d->focusClient = nullptr;
}
void WaylandKeyboard::key(std::uint32_t time, std::uint32_t keyCode, bool pressed) {
    wl_resource* target = resourceForClient(d->resources, d->focusClient);
    if (target)
        wl_keyboard_send_key(target, serial(target), time, keyCode,
                             pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
}
void WaylandKeyboard::modifiers(std::uint32_t depressed, std::uint32_t latched,
                                std::uint32_t locked, std::uint32_t group) {
    wl_resource* target = resourceForClient(d->resources, d->focusClient);
    if (target)
        wl_keyboard_send_modifiers(target, serial(target), depressed, latched, locked, group);
}

void WaylandPointer::enter(WaylandSurface* surface, float x, float y) {
    wl_resource* target = resourceForSurface(d->resources, surface);
    if (!target) return;
    d->focusClient = wl_resource_get_client(target);
    wl_pointer_send_enter(target, serial(target), surface->resource().handle(),
                          wl_fixed_from_double(x), wl_fixed_from_double(y));
}
void WaylandPointer::leave(WaylandSurface* surface) {
    wl_resource* target = resourceForSurface(d->resources, surface);
    if (!target) return;
    wl_pointer_send_leave(target, serial(target), surface->resource().handle());
    if (d->focusClient == wl_resource_get_client(target)) d->focusClient = nullptr;
}
void WaylandPointer::motion(std::uint32_t time, float x, float y) {
    wl_resource* target = resourceForClient(d->resources, d->focusClient);
    if (target)
        wl_pointer_send_motion(target, time, wl_fixed_from_double(x), wl_fixed_from_double(y));
}
void WaylandPointer::button(std::uint32_t time, std::uint32_t buttonCode, bool pressed) {
    wl_resource* target = resourceForClient(d->resources, d->focusClient);
    if (target)
        wl_pointer_send_button(target, serial(target), time, buttonCode,
            pressed ? WL_POINTER_BUTTON_STATE_PRESSED : WL_POINTER_BUTTON_STATE_RELEASED);
}
void WaylandPointer::axis(std::uint32_t time, std::uint32_t axisValue, float value) {
    wl_resource* target = resourceForClient(d->resources, d->focusClient);
    if (target)
        wl_pointer_send_axis(target, time, axisValue, wl_fixed_from_double(value));
}

void WaylandTouch::down(WaylandSurface* surface, std::uint32_t time, int id, float x, float y) {
    wl_resource* target = resourceForSurface(d->resources, surface);
    if (!target) return;
    d->focusClient = wl_resource_get_client(target);
    wl_touch_send_down(target, serial(target), time, surface->resource().handle(), id,
                       wl_fixed_from_double(x), wl_fixed_from_double(y));
}
void WaylandTouch::up(std::uint32_t time, int id) {
    wl_resource* target = resourceForClient(d->resources, d->focusClient);
    if (target) wl_touch_send_up(target, serial(target), time, id);
}
void WaylandTouch::motion(std::uint32_t time, int id, float x, float y) {
    wl_resource* target = resourceForClient(d->resources, d->focusClient);
    if (target)
        wl_touch_send_motion(target, time, id, wl_fixed_from_double(x), wl_fixed_from_double(y));
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
