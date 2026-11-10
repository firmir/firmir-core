#include <firmir/wayland/compositor/core.hpp>
#include <firmir/wayland/compositor/surface.hpp>
#include <wayland-server.h>
#include <algorithm>

namespace Firmir {
namespace Wayland {
namespace Compositor {

class WaylandObject::Impl {
public:
    std::vector<WaylandCompositorExtension*> extensions;
};

class WaylandCompositorExtension::Impl {
public:
    WaylandCompositor* compositor = nullptr;
};

class WaylandClient::Impl {
public:
    Server::Client client;
    std::vector<WaylandResource> resources;
};

class WaylandOutput::Impl {
public:
    std::string manufacturer = "Firmir";
    std::string model = "Virtual output";
    Size size{1280, 720};
    int scale = 1;
    WaylandOutputMode currentMode{1280, 720, 60000};
    std::vector<WaylandOutputMode> modes;
    wl_global* global = nullptr;
};

class WaylandCompositor::Impl {
public:
    std::unique_ptr<Server::Display> display;
    std::string socketName;
    std::vector<WaylandOutput*> outputs;
    std::vector<WaylandClient*> clients;
    std::vector<WaylandSurface*> surfaces;
    wl_global* compositorGlobal = nullptr;
    bool created = false;
};

wl_client* WaylandResource::client() const {
    const Server::Client client = d->resource.client();
    return client.handle();
}

WaylandObject::WaylandObject(Object* parent) : Object(parent), d(std::make_unique<Impl>()) {}
WaylandObject::~WaylandObject() = default;
const std::vector<WaylandCompositorExtension*>& WaylandObject::extensions() const {
    return d->extensions;
}
void WaylandObject::addExtension(WaylandCompositorExtension* extension) {
    if (!extension) return;
    d->extensions.push_back(extension);
    if (!extension->parent()) extension->setParent(this);
}

WaylandCompositorExtension::WaylandCompositorExtension(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
WaylandCompositorExtension::~WaylandCompositorExtension() = default;
WaylandCompositor* WaylandCompositorExtension::compositor() const { return d->compositor; }
void WaylandCompositorExtension::setCompositor(WaylandCompositor* compositor) {
    d->compositor = compositor;
}
bool WaylandCompositorExtension::initialize(WaylandCompositor* compositor) {
    d->compositor = compositor;
    return true;
}

WaylandClient::WaylandClient(wl_client* client, Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {
    d->client = Server::Client(client);
}
WaylandClient::~WaylandClient() = default;
wl_client* WaylandClient::handle() const { return d->client.handle(); }
void WaylandClient::credentials(pid_t& pid, uid_t& uid, gid_t& gid) const {
    d->client.credentials(pid, uid, gid);
}
void WaylandClient::flush() const { d->client.flush(); }
const std::vector<WaylandResource>& WaylandClient::resources() const { return d->resources; }
void WaylandClient::addResource(wl_resource* resource) {
    d->resources.emplace_back(resource);
}

bool WaylandBufferRef::isShm() const { return d->buffer && wl_shm_buffer_get(d->buffer); }
Size WaylandBufferRef::size() const {
    wl_shm_buffer* shm = d->buffer ? wl_shm_buffer_get(d->buffer) : nullptr;
    return shm ? Size{wl_shm_buffer_get_width(shm), wl_shm_buffer_get_height(shm)} : Size{};
}
int WaylandBufferRef::stride() const {
    wl_shm_buffer* shm = d->buffer ? wl_shm_buffer_get(d->buffer) : nullptr;
    return shm ? wl_shm_buffer_get_stride(shm) : 0;
}
const void* WaylandBufferRef::data() const {
    wl_shm_buffer* shm = d->buffer ? wl_shm_buffer_get(d->buffer) : nullptr;
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

WaylandOutput::WaylandOutput(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
std::string WaylandOutput::manufacturer() const { return d->manufacturer; }
void WaylandOutput::setManufacturer(std::string value) { d->manufacturer = std::move(value); }
std::string WaylandOutput::model() const { return d->model; }
void WaylandOutput::setModel(std::string value) { d->model = std::move(value); }
Size WaylandOutput::size() const { return d->size; }
int WaylandOutput::scale() const { return d->scale; }
void WaylandOutput::setScale(int scale) { d->scale = scale; }
WaylandOutputMode WaylandOutput::currentMode() const { return d->currentMode; }
const std::vector<WaylandOutputMode>& WaylandOutput::modes() const { return d->modes; }
void WaylandOutput::addMode(WaylandOutputMode mode) { d->modes.push_back(mode); }
WaylandOutput::~WaylandOutput() { if (d->global) wl_global_destroy(d->global); }
bool WaylandOutput::initialize(WaylandCompositor* compositor) {
    if (!compositor || !compositor->display()) return false;
    d->global = compositor->display()->createGlobal(&wl_output_interface, 4, bindOutput, this);
    return d->global != nullptr;
}
void WaylandOutput::setSize(Size size) {
    d->size = size;
    d->currentMode.width = size.width;
    d->currentMode.height = size.height;
}
void WaylandOutput::setCurrentMode(WaylandOutputMode mode) {
    d->currentMode = mode;
    d->size = {mode.width, mode.height};
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

    static void surfaceDestroyed(wl_resource* resource) {
        auto* surface = static_cast<WaylandSurface*>(wl_resource_get_user_data(resource));
        auto* compositor = static_cast<WaylandCompositor*>(surface->parent());
        // Let observers (e.g. views being painted on a render thread) detach
        // while the object is still fully valid.
        surface->emitSignal(WaylandSurface::signalAboutToBeDestroyed);
        if (compositor) {
            auto& surfaces = compositor->d->surfaces;
            surfaces.erase(std::remove(surfaces.begin(), surfaces.end(), surface),
                           surfaces.end());
        }
        delete surface;
    }
    static void createSurface(wl_client* client, wl_resource* resource, std::uint32_t id) {
        auto* compositor = static_cast<WaylandCompositor*>(wl_resource_get_user_data(resource));
        wl_resource* surfaceResource = wl_resource_create(client, &wl_surface_interface, 4, id);
        if (!surfaceResource) { wl_client_post_no_memory(client); return; }
        auto* surface = new WaylandSurface(compositor);
        surface->setResource(surfaceResource);
        compositor->d->surfaces.push_back(surface);
        wl_resource_set_implementation(surfaceResource, &surfaceImplementation, surface,
                                       surfaceDestroyed);
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
        auto found = std::find_if(compositor->d->clients.begin(), compositor->d->clients.end(),
            [client](WaylandClient* value) { return value->handle() == client; });
        WaylandClient* object = found == compositor->d->clients.end() ? nullptr : *found;
        if (!object) {
            object = new WaylandClient(client, compositor);
            compositor->d->clients.push_back(object);
        }
        object->addResource(resource);
    }
};

WaylandCompositor::WaylandCompositor(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {
    d->display = std::make_unique<Server::Display>();
}
Server::Display* WaylandCompositor::display() const { return d->display.get(); }
std::string WaylandCompositor::socketName() const { return d->socketName; }
const std::vector<WaylandOutput*>& WaylandCompositor::outputs() const { return d->outputs; }
const std::vector<WaylandClient*>& WaylandCompositor::clients() const { return d->clients; }
const std::vector<WaylandSurface*>& WaylandCompositor::surfaces() const { return d->surfaces; }
WaylandCompositor::~WaylandCompositor() {
    if (d->compositorGlobal) wl_global_destroy(d->compositorGlobal);
    // Tear down client connections first: resource destroy callbacks touch
    // the shell/surface objects, so those must still be alive here.
    if (d->display) d->display->destroyClients();
    const auto childrenCopy = children();
    for (Object* child : childrenCopy) delete child;
    d->outputs.clear(); d->clients.clear(); d->surfaces.clear();
    d->display.reset();
}
bool WaylandCompositor::create() {
    if (!d->display->valid()) return false;
    if (d->created) return true;
    d->compositorGlobal = d->display->createGlobal(&wl_compositor_interface, 4,
                                                 CompositorGlobal::bind, this);
    d->created = d->compositorGlobal != nullptr && wl_display_init_shm(d->display->handle()) == 0;
    return d->created;
}
bool WaylandCompositor::valid() const { return d->created && d->display && d->display->valid(); }
std::string WaylandCompositor::addSocketAuto() {
    d->socketName = d->display ? d->display->addSocketAuto() : "";
    return d->socketName;
}
int WaylandCompositor::exec() { return d->display ? d->display->run() : -1; }
void WaylandCompositor::terminate() { if (d->display) d->display->terminate(); }
int WaylandCompositor::dispatch(int timeoutMs) {
    const int result = d->display ? d->display->dispatch(timeoutMs) : -1;
    if (d->display) d->display->flushClients();
    return result;
}
WaylandOutput* WaylandCompositor::addOutput(Size size) {
    auto* output = new WaylandOutput(this);
    output->setSize(size);
    output->setCurrentMode({size.width, size.height, 60000});
    output->addMode(output->currentMode());
    if (!output->initialize(this)) { delete output; return nullptr; }
    d->outputs.push_back(output);
    return output;
}
} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
