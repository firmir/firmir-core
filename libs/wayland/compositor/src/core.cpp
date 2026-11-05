#include <firmir/wayland/compositor/core.hpp>
#include <firmir/wayland/compositor/surface.hpp>
#include <wayland-server.h>
#include <algorithm>

namespace Firmir {
namespace Wayland {
namespace Compositor {

wl_client* WaylandResource::client() const {
    const Server::Client client = m_resource.client();
    return client.handle();
}
WaylandObject::~WaylandObject() = default;
void WaylandObject::addExtension(WaylandCompositorExtension* extension) {
    if (!extension) return;
    m_extensions.push_back(extension);
    if (!extension->parent()) extension->setParent(this);
}
WaylandCompositorExtension::~WaylandCompositorExtension() = default;
bool WaylandCompositorExtension::initialize(WaylandCompositor* compositor) {
    m_compositor = compositor;
    return true;
}
WaylandClient::WaylandClient(wl_client* client, Object* parent)
    : WaylandObject(parent), m_client(client) {}

bool WaylandBufferRef::isShm() const { return m_buffer && wl_shm_buffer_get(m_buffer); }
Size WaylandBufferRef::size() const {
    wl_shm_buffer* shm = m_buffer ? wl_shm_buffer_get(m_buffer) : nullptr;
    return shm ? Size{wl_shm_buffer_get_width(shm), wl_shm_buffer_get_height(shm)} : Size{};
}
int WaylandBufferRef::stride() const {
    wl_shm_buffer* shm = m_buffer ? wl_shm_buffer_get(m_buffer) : nullptr;
    return shm ? wl_shm_buffer_get_stride(shm) : 0;
}
const void* WaylandBufferRef::data() const {
    wl_shm_buffer* shm = m_buffer ? wl_shm_buffer_get(m_buffer) : nullptr;
    return shm ? wl_shm_buffer_get_data(shm) : nullptr;
}

namespace {

void outputRelease(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
const struct wl_output_interface outputImplementation = {outputRelease};

void sendOutputDescription(WaylandOutput* output, wl_resource* resource, int version) {
    const Size size = output->size();
    wl_output_send_geometry(resource, 0, 0, size.width, size.height,
                            WL_OUTPUT_SUBPIXEL_UNKNOWN,
                            output->manufacturer().c_str(), output->model().c_str(),
                            WL_OUTPUT_TRANSFORM_NORMAL);
    for (const auto& mode : output->modes())
        wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT, mode.width, mode.height, mode.refreshRate);
    if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
        wl_output_send_scale(resource, output->scale());
    if (version >= WL_OUTPUT_NAME_SINCE_VERSION)
        wl_output_send_name(resource, output->model().c_str());
    if (version >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION)
        wl_output_send_description(resource, "Firmir virtual output");
    if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
        wl_output_send_done(resource);
}

void bindOutput(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
    auto* output = static_cast<WaylandOutput*>(data);
    const int bounded = std::min(4u, version);
    wl_resource* resource = wl_resource_create(client, &wl_output_interface, bounded, id);
    if (!resource) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(resource, &outputImplementation, output, nullptr);
    sendOutputDescription(output, resource, bounded);
}

} // namespace

WaylandOutput::WaylandOutput(Object* parent) : WaylandObject(parent) {}
WaylandOutput::~WaylandOutput() { if (m_global) wl_global_destroy(m_global); }
bool WaylandOutput::initialize(WaylandCompositor* compositor) {
    if (!compositor || !compositor->display()) return false;
    m_global = compositor->display()->createGlobal(&wl_output_interface, 4, bindOutput, this);
    return m_global != nullptr;
}
void WaylandOutput::setSize(Size size) {
    m_size = size;
    m_currentMode.width = size.width;
    m_currentMode.height = size.height;
}
void WaylandOutput::setCurrentMode(WaylandOutputMode mode) {
    m_currentMode = mode;
    m_size = {mode.width, mode.height};
}

struct CompositorGlobal {
    static WaylandSurface* surface(wl_resource* resource) {
        return static_cast<WaylandSurface*>(wl_resource_get_user_data(resource));
    }
    static void surfaceDestroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
    static void attach(wl_client*, wl_resource* resource, wl_resource* buffer,
                       std::int32_t x, std::int32_t y) { surface(resource)->attach(buffer, x, y); }
    static void damage(wl_client*, wl_resource* resource, std::int32_t x, std::int32_t y,
                       std::int32_t width, std::int32_t height) {
        surface(resource)->addDamage({x, y, width, height});
    }
    static void frame(wl_client* client, wl_resource*, std::uint32_t id) {
        wl_resource* callback = wl_resource_create(client, &wl_callback_interface, 1, id);
        if (callback) {
            wl_callback_send_done(callback, 0);
            wl_resource_destroy(callback);
        }
    }
    static void setRegion(wl_client*, wl_resource*, wl_resource*) {}
    static void commit(wl_client*, wl_resource* resource) { surface(resource)->commit(); }
    static void setTransform(wl_client*, wl_resource*, std::int32_t) {}
    static void setScale(wl_client*, wl_resource*, std::int32_t) {}
    static void offset(wl_client*, wl_resource*, std::int32_t, std::int32_t) {}
    inline static const struct wl_surface_interface surfaceImplementation = {
        surfaceDestroy, attach, damage, frame, setRegion, setRegion, commit,
        setTransform, setScale, damage, offset
    };

    static void regionDestroy(wl_client*, wl_resource* resource) { wl_resource_destroy(resource); }
    static void regionOp(wl_client*, wl_resource*, std::int32_t, std::int32_t,
                         std::int32_t, std::int32_t) {}
    inline static const struct wl_region_interface regionImplementation = {
        regionDestroy, regionOp, regionOp
    };

    static void createSurface(wl_client* client, wl_resource* resource, std::uint32_t id) {
        auto* compositor = static_cast<WaylandCompositor*>(wl_resource_get_user_data(resource));
        wl_resource* surfaceResource = wl_resource_create(client, &wl_surface_interface, 4, id);
        if (!surfaceResource) { wl_client_post_no_memory(client); return; }
        auto* surface = new WaylandSurface(compositor);
        surface->setResource(surfaceResource);
        compositor->m_surfaces.push_back(surface);
        wl_resource_set_implementation(surfaceResource, &surfaceImplementation, surface, nullptr);
    }
    static void createRegion(wl_client* client, wl_resource*, std::uint32_t id) {
        wl_resource* region = wl_resource_create(client, &wl_region_interface, 1, id);
        if (region) wl_resource_set_implementation(region, &regionImplementation, nullptr, nullptr);
        else wl_client_post_no_memory(client);
    }
    inline static const struct wl_compositor_interface compositorImplementation = {
        createSurface, createRegion
    };
    static void bind(wl_client* client, void* data, std::uint32_t version, std::uint32_t id) {
        auto* compositor = static_cast<WaylandCompositor*>(data);
        wl_resource* resource = wl_resource_create(client, &wl_compositor_interface,
                                                   std::min(4u, version), id);
        if (!resource) { wl_client_post_no_memory(client); return; }
        wl_resource_set_implementation(resource, &compositorImplementation, compositor, nullptr);
        auto found = std::find_if(compositor->m_clients.begin(), compositor->m_clients.end(),
            [client](WaylandClient* value) { return value->handle() == client; });
        WaylandClient* object = found == compositor->m_clients.end() ? nullptr : *found;
        if (!object) {
            object = new WaylandClient(client, compositor);
            compositor->m_clients.push_back(object);
        }
        object->addResource(resource);
    }
};

WaylandCompositor::WaylandCompositor(Object* parent)
    : WaylandObject(parent), m_display(std::make_unique<Server::Display>()) {}
WaylandCompositor::~WaylandCompositor() {
    if (m_compositorGlobal) wl_global_destroy(m_compositorGlobal);
    const auto childrenCopy = children();
    for (Object* child : childrenCopy) delete child;
    m_outputs.clear(); m_clients.clear(); m_surfaces.clear();
    m_display.reset();
}
bool WaylandCompositor::create() {
    if (!m_display->valid()) return false;
    if (m_created) return true;
    m_compositorGlobal = m_display->createGlobal(&wl_compositor_interface, 4,
                                                 CompositorGlobal::bind, this);
    m_created = m_compositorGlobal != nullptr && wl_display_init_shm(m_display->handle()) == 0;
    return m_created;
}
bool WaylandCompositor::valid() const { return m_created && m_display && m_display->valid(); }
std::string WaylandCompositor::addSocketAuto() {
    m_socketName = m_display ? m_display->addSocketAuto() : "";
    return m_socketName;
}
int WaylandCompositor::exec() { return m_display ? m_display->run() : -1; }
void WaylandCompositor::terminate() { if (m_display) m_display->terminate(); }
int WaylandCompositor::dispatch(int timeoutMs) {
    const int result = m_display ? m_display->dispatch(timeoutMs) : -1;
    if (m_display) m_display->flushClients();
    return result;
}
WaylandOutput* WaylandCompositor::addOutput(Size size) {
    auto* output = new WaylandOutput(this);
    output->setSize(size);
    output->setCurrentMode({size.width, size.height, 60000});
    output->addMode(output->currentMode());
    if (!output->initialize(this)) { delete output; return nullptr; }
    m_outputs.push_back(output);
    return output;
}
} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
