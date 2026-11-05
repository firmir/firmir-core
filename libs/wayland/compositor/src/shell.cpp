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

WaylandShellSurface::~WaylandShellSurface() = default;
void WaylandShellSurface::sendConfigure(int width, int height) { m_size = {width, height}; }
void WaylandWlShellSurface::sendConfigure(int width, int height) {
    setSize({width, height});
    if (m_resource.handle())
        wl_shell_surface_send_configure(m_resource.handle(), WL_SHELL_SURFACE_RESIZE_BOTTOM_RIGHT,
                                        width, height);
}
void WaylandXdgPopup::sendConfigure(int width, int height) {
    setSize({width, height});
    if (m_resource.handle())
        xdg_popup_send_configure(m_resource.handle(), 0, 0, width, height);
}
void WaylandXdgPopup::sendPopupDone() {
    if (m_resource.handle()) xdg_popup_send_popup_done(m_resource.handle());
}
void WaylandXdgToplevel::sendConfigure(int width, int height) {
    setSize({width, height});
    if (!m_resource.handle()) return;
    wl_array states{};
    std::uint32_t maximized = XDG_TOPLEVEL_STATE_MAXIMIZED;
    std::uint32_t fullscreen = XDG_TOPLEVEL_STATE_FULLSCREEN;
    if (m_fullscreen) { states.data = &fullscreen; states.size = states.alloc = sizeof(fullscreen); }
    else if (m_maximized) { states.data = &maximized; states.size = states.alloc = sizeof(maximized); }
    xdg_toplevel_send_configure(m_resource.handle(), width, height, &states);
}
void WaylandXdgToplevel::sendClose() {
    if (m_resource.handle()) xdg_toplevel_send_close(m_resource.handle());
}
void WaylandXdgToplevel::setMaximized(bool maximized) {
    m_maximized = maximized;
    if (!maximized) m_fullscreen = false;
    sendConfigure(m_size.width, m_size.height);
}
void WaylandXdgToplevel::setFullscreen(bool fullscreen) {
    m_fullscreen = fullscreen;
    sendConfigure(m_size.width, m_size.height);
}
void WaylandXdgToplevel::setMinimized() {}
void WaylandXdgSurface::sendConfigure(int width, int height) {
    setSize({width, height});
    if (m_toplevel) m_toplevel->sendConfigure(width, height);
    if (m_resource.handle()) {
        const std::uint32_t serial = wl_display_next_serial(
            wl_client_get_display(wl_resource_get_client(m_resource.handle())));
        xdg_surface_send_configure(m_resource.handle(), serial);
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

WaylandWlShell::~WaylandWlShell() { if (m_global) wl_global_destroy(m_global); }
bool WaylandWlShell::initialize(WaylandCompositor* compositor) {
    if (!WaylandShell::initialize(compositor)) return false;
    m_global = compositor->display()->createGlobal(&wl_shell_interface, 1, bindWlShell, this);
    return m_global != nullptr;
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
        shell->m_resources.push_back(resource);
    }
};

WaylandXdgShell::~WaylandXdgShell() { if (m_global) wl_global_destroy(m_global); }
bool WaylandXdgShell::initialize(WaylandCompositor* compositor) {
    if (!WaylandShell::initialize(compositor)) return false;
    m_global = compositor->display()->createGlobal(&xdg_wm_base_interface, 6,
                                                   XdgShellGlobal::bind, this);
    return m_global != nullptr;
}
void WaylandXdgShell::ping(std::uint32_t serialValue) {
    for (wl_resource* resource : m_resources) xdg_wm_base_send_ping(resource, serialValue);
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

WaylandViewporter::~WaylandViewporter() { if (m_global) wl_global_destroy(m_global); }
bool WaylandViewporter::initialize(WaylandCompositor* compositor) {
    if (!WaylandCompositorExtension::initialize(compositor)) return false;
    m_global = compositor->display()->createGlobal(&wp_viewporter_interface, 1, bindViewporter, this);
    return m_global != nullptr;
}
WaylandXdgDecorationManagerV1::~WaylandXdgDecorationManagerV1() {
    if (m_global) wl_global_destroy(m_global);
}
bool WaylandXdgDecorationManagerV1::initialize(WaylandCompositor* compositor) {
    if (!WaylandCompositorExtension::initialize(compositor)) return false;
    m_global = compositor->display()->createGlobal(&zxdg_decoration_manager_v1_interface, 1,
                                                   bindDecoration, this);
    return m_global != nullptr;
}
WaylandXdgOutputManagerV1::~WaylandXdgOutputManagerV1() {
    if (m_global) wl_global_destroy(m_global);
}
bool WaylandXdgOutputManagerV1::initialize(WaylandCompositor* compositor) {
    if (!WaylandCompositorExtension::initialize(compositor)) return false;
    m_global = compositor->display()->createGlobal(&zxdg_output_manager_v1_interface, 3,
                                                   bindXdgOutputManager, this);
    return m_global != nullptr;
}
WaylandXdgOutputV1* WaylandXdgOutputManagerV1::addOutput(WaylandOutput* output) {
    auto* wrapper = new WaylandXdgOutputV1(this);
    wrapper->setOutput(output);
    m_outputs.push_back(wrapper);
    return wrapper;
}
void WaylandXdgOutputV1::sendDescription() {
    if (!m_resource.handle() || !m_output) return;
    wl_resource* resource = m_resource.handle();
    const Size size = m_output->size();
    zxdg_output_v1_send_logical_position(resource, 0, 0);
    zxdg_output_v1_send_logical_size(resource, size.width, size.height);
    if (m_resource.version() >= 2) {
        zxdg_output_v1_send_name(resource, m_output->model().c_str());
        zxdg_output_v1_send_description(resource, "Firmir virtual output");
    }
    zxdg_output_v1_send_done(resource);
}
} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
