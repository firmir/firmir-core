#include <firmir/wayland/compositor/shell.hpp>
#include <wayland-server.h>
#include <xdg-shell-server-protocol.h>
#include <viewporter-server-protocol.h>
#include <xdg-decoration-unstable-v1-server-protocol.h>
#include <xdg-output-unstable-v1-server-protocol.h>
#include <algorithm>
#include <cstring>

namespace Firmir {
namespace Wayland {
namespace Compositor {

class WaylandShellSurface::Impl {
public:
    WaylandResource resource;
    WaylandSurface* surface = nullptr;
    std::string title;
    Size size;
};

class WaylandShell::Impl {
public:
    std::vector<WaylandShellSurface*> surfaces;
};

class WaylandWlShell::Impl {
public:
    wl_global* global = nullptr;
};

class WaylandXdgToplevel::Impl {
public:
    bool maximized = false;
    bool fullscreen = false;
};

class WaylandXdgSurface::Impl {
public:
    WaylandXdgToplevel* toplevel = nullptr;
    WaylandXdgPopup* popup = nullptr;
};

class WaylandXdgShell::Impl {
public:
    std::vector<wl_resource*> resources;
    wl_global* global = nullptr;
};

class WaylandXdgDecorationManagerV1::Impl {
public:
    Mode defaultMode = Mode::ClientSide;
    wl_global* global = nullptr;
};

class WaylandXdgOutputV1::Impl {
public:
    WaylandOutput* output = nullptr;
    WaylandResource resource;
};

class WaylandXdgOutputManagerV1::Impl {
public:
    std::vector<WaylandXdgOutputV1*> outputs;
    wl_global* global = nullptr;
};

class WaylandViewporter::Impl {
public:
    wl_global* global = nullptr;
};

WaylandShellSurface::WaylandShellSurface(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
WaylandShellSurface::~WaylandShellSurface() = default;
WaylandResource WaylandShellSurface::resource() const { return d->resource; }
void WaylandShellSurface::setResource(wl_resource* resource) {
    d->resource = WaylandResource(resource);
}
WaylandSurface* WaylandShellSurface::surface() const { return d->surface; }
void WaylandShellSurface::setSurface(WaylandSurface* surface) { d->surface = surface; }
std::string WaylandShellSurface::title() const { return d->title; }
void WaylandShellSurface::setTitle(std::string title) { d->title = std::move(title); }
Size WaylandShellSurface::size() const { return d->size; }
void WaylandShellSurface::setSize(Size size) { d->size = size; }

WaylandShell::WaylandShell(Object* parent)
    : WaylandCompositorExtension(parent), d(std::make_unique<Impl>()) {}
WaylandShell::~WaylandShell() = default;
std::vector<WaylandShellSurface*> WaylandShell::shellSurfaces() const { return d->surfaces; }
void WaylandShell::registerSurface(WaylandShellSurface* surface) { d->surfaces.push_back(surface); }

WaylandWlShell::WaylandWlShell(Object* parent)
    : WaylandShell(parent), d(std::make_unique<Impl>()) {}

WaylandXdgToplevel::WaylandXdgToplevel(Object* parent)
    : WaylandShellSurface(parent), d(std::make_unique<Impl>()) {}
WaylandXdgToplevel::~WaylandXdgToplevel() = default;
bool WaylandXdgToplevel::maximized() const { return d->maximized; }
bool WaylandXdgToplevel::fullscreen() const { return d->fullscreen; }

WaylandXdgSurface::WaylandXdgSurface(Object* parent)
    : WaylandShellSurfaceTemplate<>(parent), d(std::make_unique<Impl>()) {}
WaylandXdgSurface::~WaylandXdgSurface() = default;
WaylandXdgToplevel* WaylandXdgSurface::toplevel() const { return d->toplevel; }
void WaylandXdgSurface::setToplevel(WaylandXdgToplevel* toplevel) { d->toplevel = toplevel; }
WaylandXdgPopup* WaylandXdgSurface::popup() const { return d->popup; }
void WaylandXdgSurface::setPopup(WaylandXdgPopup* popup) { d->popup = popup; }

WaylandXdgShell::WaylandXdgShell(Object* parent)
    : WaylandShell(parent), d(std::make_unique<Impl>()) {}

WaylandXdgDecorationManagerV1::WaylandXdgDecorationManagerV1(Object* parent)
    : WaylandCompositorExtension(parent), d(std::make_unique<Impl>()) {}
void WaylandXdgDecorationManagerV1::setDefaultMode(Mode mode) { d->defaultMode = mode; }
WaylandXdgDecorationManagerV1::Mode WaylandXdgDecorationManagerV1::defaultMode() const {
    return d->defaultMode;
}

WaylandXdgOutputV1::WaylandXdgOutputV1(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
WaylandXdgOutputV1::~WaylandXdgOutputV1() = default;
WaylandOutput* WaylandXdgOutputV1::output() const { return d->output; }
void WaylandXdgOutputV1::setOutput(WaylandOutput* output) { d->output = output; }
WaylandResource WaylandXdgOutputV1::resource() const { return d->resource; }
void WaylandXdgOutputV1::setResource(wl_resource* resource) {
    d->resource = WaylandResource(resource);
}

WaylandXdgOutputManagerV1::WaylandXdgOutputManagerV1(Object* parent)
    : WaylandCompositorExtension(parent), d(std::make_unique<Impl>()) {}
const std::vector<WaylandXdgOutputV1*>& WaylandXdgOutputManagerV1::outputs() const {
    return d->outputs;
}

WaylandViewporter::WaylandViewporter(Object* parent)
    : WaylandCompositorExtension(parent), d(std::make_unique<Impl>()) {}

void WaylandShellSurface::sendConfigure(int width, int height) { setSize({width, height}); }
void WaylandWlShellSurface::sendConfigure(int width, int height) {
    setSize({width, height});
    if (resource().handle())
        wl_shell_surface_send_configure(resource().handle(), WL_SHELL_SURFACE_RESIZE_BOTTOM_RIGHT,
                                        width, height);
}
void WaylandXdgPopup::sendConfigure(int width, int height) {
    setSize({width, height});
    if (resource().handle())
        xdg_popup_send_configure(resource().handle(), 0, 0, width, height);
}
void WaylandXdgPopup::sendPopupDone() {
    if (resource().handle()) xdg_popup_send_popup_done(resource().handle());
}
void WaylandXdgToplevel::sendConfigure(int width, int height) {
    setSize({width, height});
    if (!resource().handle()) return;
    wl_array states{};
    std::uint32_t maximized = XDG_TOPLEVEL_STATE_MAXIMIZED;
    std::uint32_t fullscreen = XDG_TOPLEVEL_STATE_FULLSCREEN;
    if (d->fullscreen) { states.data = &fullscreen; states.size = states.alloc = sizeof(fullscreen); }
    else if (d->maximized) { states.data = &maximized; states.size = states.alloc = sizeof(maximized); }
    xdg_toplevel_send_configure(resource().handle(), width, height, &states);
}
void WaylandXdgToplevel::sendClose() {
    if (resource().handle()) xdg_toplevel_send_close(resource().handle());
}
void WaylandXdgToplevel::setMaximized(bool maximized) {
    d->maximized = maximized;
    if (!maximized) d->fullscreen = false;
    sendConfigure(size().width, size().height);
}
void WaylandXdgToplevel::setFullscreen(bool fullscreen) {
    d->fullscreen = fullscreen;
    sendConfigure(size().width, size().height);
}
void WaylandXdgToplevel::setMinimized() {}
void WaylandXdgSurface::sendConfigure(int width, int height) {
    setSize({width, height});
    if (d->toplevel) d->toplevel->sendConfigure(width, height);
    if (resource().handle()) {
        const std::uint32_t serial = wl_display_next_serial(
            wl_client_get_display(wl_resource_get_client(resource().handle())));
        xdg_surface_send_configure(resource().handle(), serial);
    }
}

namespace {

WaylandSurface* findSurface(WaylandCompositor* compositor, wl_object* object) {
    if (!compositor) return nullptr;
    for (WaylandSurface* surface : compositor->surfaces())
        if (reinterpret_cast<wl_object*>(surface->resource().handle()) == object) return surface;
    return nullptr;
}

struct LegacyContext {
    WaylandWlShell* shell = nullptr;
    WaylandWlShellSurface* surface = nullptr;
};
void destroyLegacy(wl_resource* resource) {
    delete static_cast<LegacyContext*>(wl_resource_get_user_data(resource));
}
int legacyDispatch(const void*, void* target, std::uint32_t, const wl_message* message,
                   wl_argument* args) {
    auto* resource = static_cast<wl_resource*>(target);
    auto* context = static_cast<LegacyContext*>(wl_resource_get_user_data(resource));
    if (std::strcmp(message->name, "destroy") == 0) {
        wl_resource_destroy(resource);
    } else if (std::strcmp(message->name, "get_shell_surface") == 0) {
        auto* surface = new WaylandWlShellSurface(context->shell);
        wl_resource* shellResource = wl_resource_create(wl_resource_get_client(resource),
            &wl_shell_surface_interface, 1, args[0].n);
        surface->setResource(shellResource);
        surface->setSurface(findSurface(context->shell->compositor(), args[1].o));
        auto* child = new LegacyContext{context->shell, surface};
        wl_resource_set_dispatcher(shellResource, legacyDispatch, child, child, destroyLegacy);
        context->shell->registerSurface(surface);
    } else if (context->surface && std::strcmp(message->name, "set_title") == 0) {
        context->surface->setTitle(args[0].s ? args[0].s : "");
    }
    return 0;
}
void bindWlShell(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
    auto* shell = static_cast<WaylandWlShell*>(data);
    wl_resource* resource = wl_resource_create(client, &wl_shell_interface, 1, id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    auto* context = new LegacyContext{shell, nullptr};
    wl_resource_set_dispatcher(resource, legacyDispatch, context, context, destroyLegacy);
    (void)version;
}

} // namespace

WaylandWlShell::~WaylandWlShell() { if (d->global) wl_global_destroy(d->global); }
bool WaylandWlShell::initialize(WaylandCompositor* compositor) {
    if (!WaylandShell::initialize(compositor)) return false;
    d->global = compositor->display()->createGlobal(&wl_shell_interface, 1, bindWlShell, this);
    return d->global != nullptr;
}

struct XdgShellGlobal {
    struct Context {
        WaylandXdgShell* shell = nullptr;
        WaylandShellSurface* object = nullptr;
    };
    static void destroyContext(wl_resource* resource) {
        delete static_cast<Context*>(wl_resource_get_user_data(resource));
    }
    static void install(wl_resource* resource, WaylandXdgShell* shell,
                        WaylandShellSurface* object, wl_dispatcher_func_t dispatch) {
        auto* context = new Context{shell, object};
        wl_resource_set_dispatcher(resource, dispatch, context, context, destroyContext);
    }
    static int dispatch(const void*, void* target, std::uint32_t,
                        const wl_message* message, wl_argument* args) {
        auto* resource = static_cast<wl_resource*>(target);
        auto* context = static_cast<Context*>(wl_resource_get_user_data(resource));
        WaylandXdgShell* shell = context->shell;
        const char* name = message->name;
        if (std::strcmp(name, "destroy") == 0) {
            wl_resource_destroy(resource);
        } else if (std::strcmp(name, "create_positioner") == 0) {
            wl_resource* positioner = wl_resource_create(wl_resource_get_client(resource),
                &xdg_positioner_interface, wl_resource_get_version(resource), args[0].n);
            if (positioner) install(positioner, shell, nullptr, dispatch);
        } else if (std::strcmp(name, "get_xdg_surface") == 0) {
            auto* surface = new WaylandXdgSurface(shell);
            wl_resource* created = wl_resource_create(wl_resource_get_client(resource),
                &xdg_surface_interface, wl_resource_get_version(resource), args[0].n);
            surface->setResource(created);
            surface->setSurface(findSurface(shell->compositor(), args[1].o));
            install(created, shell, surface, dispatch);
            shell->registerSurface(surface);
        } else if (std::strcmp(name, "get_toplevel") == 0) {
            auto* xdg = dynamic_cast<WaylandXdgSurface*>(context->object);
            auto* toplevel = new WaylandXdgToplevel(shell);
            wl_resource* created = wl_resource_create(wl_resource_get_client(resource),
                &xdg_toplevel_interface, wl_resource_get_version(resource), args[0].n);
            toplevel->setResource(created);
            toplevel->setSurface(xdg ? xdg->surface() : nullptr);
            if (xdg) xdg->setToplevel(toplevel);
            install(created, shell, toplevel, dispatch);
            shell->registerSurface(toplevel);
            if (xdg) xdg->sendConfigure(0, 0);
        } else if (std::strcmp(name, "get_popup") == 0) {
            auto* xdg = dynamic_cast<WaylandXdgSurface*>(context->object);
            auto* popup = new WaylandXdgPopup(shell);
            wl_resource* created = wl_resource_create(wl_resource_get_client(resource),
                &xdg_popup_interface, wl_resource_get_version(resource), args[0].n);
            popup->setResource(created);
            popup->setSurface(xdg ? xdg->surface() : nullptr);
            if (xdg) xdg->setPopup(popup);
            install(created, shell, popup, dispatch);
            shell->registerSurface(popup);
        } else if (auto* toplevel = dynamic_cast<WaylandXdgToplevel*>(context->object)) {
            if (std::strcmp(name, "set_title") == 0)
                toplevel->setTitle(args[0].s ? args[0].s : "");
            else if (std::strcmp(name, "set_maximized") == 0) toplevel->setMaximized(true);
            else if (std::strcmp(name, "unset_maximized") == 0) toplevel->setMaximized(false);
            else if (std::strcmp(name, "set_fullscreen") == 0) toplevel->setFullscreen(true);
            else if (std::strcmp(name, "unset_fullscreen") == 0) toplevel->setFullscreen(false);
            else if (std::strcmp(name, "set_minimized") == 0) toplevel->setMinimized();
        }
        return 0;
    }
    static void bind(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
        auto* shell = static_cast<WaylandXdgShell*>(data);
        wl_resource* resource = wl_resource_create(client, &xdg_wm_base_interface,
                                                   std::min(6u, version), id);
        if (!resource) { wl_client_post_no_memory(client); return; }
        install(resource, shell, nullptr, dispatch);
        shell->d->resources.push_back(resource);
    }
};

WaylandXdgShell::~WaylandXdgShell() { if (d->global) wl_global_destroy(d->global); }
bool WaylandXdgShell::initialize(WaylandCompositor* compositor) {
    if (!WaylandShell::initialize(compositor)) return false;
    d->global = compositor->display()->createGlobal(&xdg_wm_base_interface, 6,
                                                   XdgShellGlobal::bind, this);
    return d->global != nullptr;
}
void WaylandXdgShell::ping(std::uint32_t serialValue) {
    for (wl_resource* resource : d->resources) xdg_wm_base_send_ping(resource, serialValue);
}

namespace {

int viewporterDispatch(const void*, void* target, std::uint32_t,
                       const wl_message* message, wl_argument* args) {
    auto* resource = static_cast<wl_resource*>(target);
    if (std::strcmp(message->name, "destroy") == 0) {
        wl_resource_destroy(resource);
    } else if (std::strcmp(message->name, "get_viewport") == 0) {
        wl_resource* viewport = wl_resource_create(wl_resource_get_client(resource),
            &wp_viewport_interface, 1, args[0].n);
        if (viewport) wl_resource_set_dispatcher(viewport, viewporterDispatch, nullptr, nullptr, nullptr);
    }
    return 0;
}
void bindViewporter(wl_client* client, void*, std::uint32_t version, std::uint32_t id) {
    wl_resource* resource = wl_resource_create(client, &wp_viewporter_interface, 1, id);
    if (resource) wl_resource_set_dispatcher(resource, viewporterDispatch, nullptr, nullptr, nullptr);
    else wl_client_post_no_memory(client);
    (void)version;
}

int decorationDispatch(const void*, void* target, std::uint32_t,
                       const wl_message* message, wl_argument* args) {
    auto* resource = static_cast<wl_resource*>(target);
    auto* manager = static_cast<WaylandXdgDecorationManagerV1*>(wl_resource_get_user_data(resource));
    if (std::strcmp(message->name, "destroy") == 0) {
        wl_resource_destroy(resource);
    } else if (std::strcmp(message->name, "get_xdg_toplevel_decoration") == 0) {
        wl_resource* decoration = wl_resource_create(wl_resource_get_client(resource),
            &zxdg_toplevel_decoration_v1_interface, 1, args[0].n);
        if (decoration) {
            wl_resource_set_dispatcher(decoration, decorationDispatch, manager, manager, nullptr);
            zxdg_toplevel_decoration_v1_send_configure(decoration,
                static_cast<std::uint32_t>(manager->defaultMode()));
        }
    }
    return 0;
}
void bindDecoration(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
    auto* manager = static_cast<WaylandXdgDecorationManagerV1*>(data);
    wl_resource* resource = wl_resource_create(client, &zxdg_decoration_manager_v1_interface, 1, id);
    if (resource) wl_resource_set_dispatcher(resource, decorationDispatch, manager, manager, nullptr);
    else wl_client_post_no_memory(client);
    (void)version;
}

int xdgOutputDispatch(const void*, void* target, std::uint32_t,
                      const wl_message* message, wl_argument* args) {
    auto* resource = static_cast<wl_resource*>(target);
    auto* manager = static_cast<WaylandXdgOutputManagerV1*>(wl_resource_get_user_data(resource));
    if (std::strcmp(message->name, "destroy") == 0) {
        wl_resource_destroy(resource);
    } else if (std::strcmp(message->name, "get_xdg_output") == 0) {
        WaylandOutput* output = manager->outputs().empty()
            ? (manager->compositor()->outputs().empty() ? nullptr : manager->compositor()->outputs().front())
            : manager->outputs().front()->output();
        auto* wrapper = manager->addOutput(output);
        wl_resource* created = wl_resource_create(wl_resource_get_client(resource),
            &zxdg_output_v1_interface, std::min(3, wl_resource_get_version(resource)), args[0].n);
        wrapper->setResource(created);
        if (created) wl_resource_set_dispatcher(created, xdgOutputDispatch, manager, manager, nullptr);
        wrapper->sendDescription();
    }
    return 0;
}
void bindXdgOutputManager(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
    auto* manager = static_cast<WaylandXdgOutputManagerV1*>(data);
    wl_resource* resource = wl_resource_create(client, &zxdg_output_manager_v1_interface,
                                               std::min(3u, version), id);
    if (resource) wl_resource_set_dispatcher(resource, xdgOutputDispatch, manager, manager, nullptr);
    else wl_client_post_no_memory(client);
}

} // namespace

WaylandViewporter::~WaylandViewporter() { if (d->global) wl_global_destroy(d->global); }
bool WaylandViewporter::initialize(WaylandCompositor* compositor) {
    if (!WaylandCompositorExtension::initialize(compositor)) return false;
    d->global = compositor->display()->createGlobal(&wp_viewporter_interface, 1, bindViewporter, this);
    return d->global != nullptr;
}
WaylandXdgDecorationManagerV1::~WaylandXdgDecorationManagerV1() {
    if (d->global) wl_global_destroy(d->global);
}
bool WaylandXdgDecorationManagerV1::initialize(WaylandCompositor* compositor) {
    if (!WaylandCompositorExtension::initialize(compositor)) return false;
    d->global = compositor->display()->createGlobal(&zxdg_decoration_manager_v1_interface, 1,
                                                   bindDecoration, this);
    return d->global != nullptr;
}
WaylandXdgOutputManagerV1::~WaylandXdgOutputManagerV1() {
    if (d->global) wl_global_destroy(d->global);
}
bool WaylandXdgOutputManagerV1::initialize(WaylandCompositor* compositor) {
    if (!WaylandCompositorExtension::initialize(compositor)) return false;
    d->global = compositor->display()->createGlobal(&zxdg_output_manager_v1_interface, 3,
                                                   bindXdgOutputManager, this);
    return d->global != nullptr;
}
WaylandXdgOutputV1* WaylandXdgOutputManagerV1::addOutput(WaylandOutput* output) {
    auto* wrapper = new WaylandXdgOutputV1(this);
    wrapper->setOutput(output);
    d->outputs.push_back(wrapper);
    return wrapper;
}
void WaylandXdgOutputV1::sendDescription() {
    if (!resource().handle() || !d->output) return;
    wl_resource* handle = resource().handle();
    const Size size = d->output->size();
    zxdg_output_v1_send_logical_position(handle, 0, 0);
    zxdg_output_v1_send_logical_size(handle, size.width, size.height);
    if (resource().version() >= 2) {
        zxdg_output_v1_send_name(handle, d->output->model().c_str());
        zxdg_output_v1_send_description(handle, "Firmir virtual output");
    }
    zxdg_output_v1_send_done(handle);
}
} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
