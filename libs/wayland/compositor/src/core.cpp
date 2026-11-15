#include <firmir/wayland/compositor/bufferbackend.hpp>
#include <firmir/wayland/compositor/core.hpp>
#include <firmir/wayland/compositor/surface.hpp>
#include <wayland-server.h>
#include <algorithm>
#include <mutex>

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
    /// wl_client has no destroy signal of its own for the compositor's side, so
    /// a listener is registered per connection: it deletes the WaylandClient
    /// (and drops it from WaylandCompositor::clients()) when the connection
    /// ends. Without it the client list — and every resource it referenced —
    /// grew with each client that ever connected.
    wl_listener destroyListener{};
    bool watching = false;
    WaylandClient* owner = nullptr;
    WaylandCompositor* compositor = nullptr;
};

class WaylandOutput::Impl {
public:
    std::string manufacturer = "Firmir";
    std::string model = "Virtual output";
    Size size{1280, 720};
    /// Physical panel size in millimetres; {0, 0} when unknown, which is what
    /// wl_output.geometry reports for a compositor that has no panel size
    /// (clients then fall back to their default density).
    Size physicalSize{0, 0};
    int scale = 1;
    WaylandOutputMode currentMode{1280, 720, 60000};
    std::vector<WaylandOutputMode> modes;
    wl_global* global = nullptr;
    /// wl_output resources the clients bound (see WaylandCompositor::removeOutput).
    std::vector<wl_resource*> resources;
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
    d->owner = this;
}
WaylandClient::~WaylandClient() {
    if (d->watching) {
        // Still connected (the compositor object is being torn down, or the
        // app keeps a client object alive by itself): stop watching so the
        // listener never fires on freed memory. libwayland unlinks a listener
        // itself when it does fire, hence the flag.
        wl_list_remove(&d->destroyListener.link);
        d->watching = false;
    }
}
wl_client* WaylandClient::handle() const { return d->client.handle(); }
void WaylandClient::credentials(pid_t& pid, uid_t& uid, gid_t& gid) const {
    d->client.credentials(pid, uid, gid);
}
void WaylandClient::flush() const { d->client.flush(); }
const std::vector<WaylandResource>& WaylandClient::resources() const { return d->resources; }
void WaylandClient::addResource(wl_resource* resource) {
    d->resources.emplace_back(resource);
}

void WaylandBufferRef::Impl::destroyed(wl_listener* listener, void*) {
    Impl* impl = wl_container_of(listener, impl, destroyListener);
    // The pixels of a wl_shm buffer live in the client's mapping and libwayland
    // unmaps the pool right after this returns: wait for a copy that is in
    // flight, or it would read memory that is gone (a client that disconnects
    // destroys its buffers while the render thread paints the last frame).
    std::unique_lock<std::shared_mutex> waitForReaders(impl->accessMutex);
    impl->buffer = nullptr;
    // libwayland unlinks a destroy listener itself when it fires.
    impl->watching = false;
}

std::shared_lock<std::shared_mutex> WaylandBufferRef::readLock() const {
    return std::shared_lock<std::shared_mutex>(d->accessMutex);
}

WaylandBufferRef::Impl::~Impl() {
    if (watching && buffer)
        wl_list_remove(&destroyListener.link);
}

WaylandBufferRef::WaylandBufferRef(wl_resource* buffer) : d(std::make_shared<Impl>()) {
    if (!buffer)
        return;
    d->buffer = buffer;
    d->destroyListener.notify = &Impl::destroyed;
    wl_resource_add_destroy_listener(buffer, &d->destroyListener);
    d->watching = true;
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

} // namespace

/// Bookkeeping of the wl_output resources a client bound, so that removing an
/// output can clear their user data before the WaylandOutput is deleted.
/// WaylandOutput::Impl is private, hence the friend declaration in core.hpp.
struct OutputBindings {
    static void add(WaylandOutput* output, wl_resource* resource) {
        if (output && resource)
            output->d->resources.push_back(resource);
    }
    static void remove(WaylandOutput* output, wl_resource* resource) {
        if (!output)
            return;
        auto& resources = output->d->resources;
        resources.erase(std::remove(resources.begin(), resources.end(), resource),
                        resources.end());
    }
};

namespace {

void destroyOutputBinding(wl_resource* resource) {
    OutputBindings::remove(static_cast<WaylandOutput*>(wl_resource_get_user_data(resource)),
                           resource);
}

void sendOutputDescription(WaylandOutput* output, wl_resource* resource, int version) {
    // Physical size in millimetres, not the pixel mode: a client derives its
    // millimetre-based layout from it, and reporting the pixel size makes a
    // dense panel look like a huge and very coarse one.
    const Size physical = output->physicalSize();
    wl_output_send_geometry(resource, 0, 0, physical.width, physical.height,
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
    wl_resource_set_implementation(resource, &outputImplementation, output,
                                   destroyOutputBinding);
    OutputBindings::add(output, resource);
    sendOutputDescription(output, resource, bounded);
}

} // namespace

namespace {

/// Finds the wl_output resource this client bound for a given output.
struct OutputBinding {
    const void* output;
    wl_resource* resource;
};

wl_iterator_result findOutputBinding(wl_resource* resource, void* data) {
    auto* binding = static_cast<OutputBinding*>(data);
    if (!wl_resource_instance_of(resource, &wl_output_interface, &outputImplementation))
        return WL_ITERATOR_CONTINUE;
    if (wl_resource_get_user_data(resource) != binding->output)
        return WL_ITERATOR_CONTINUE;
    binding->resource = resource;
    return WL_ITERATOR_STOP;
}

wl_resource* outputResourceFor(wl_resource* surfaceResource, const void* output) {
    if (!surfaceResource || !output)
        return nullptr;
    OutputBinding binding{output, nullptr};
    wl_client_for_each_resource(wl_resource_get_client(surfaceResource),
                                findOutputBinding, &binding);
    return binding.resource;
}

} // namespace

WaylandOutput::WaylandOutput(Object* parent)
    : WaylandObject(parent), d(std::make_unique<Impl>()) {}
std::string WaylandOutput::manufacturer() const { return d->manufacturer; }
void WaylandOutput::setManufacturer(std::string value) { d->manufacturer = std::move(value); }
std::string WaylandOutput::model() const { return d->model; }
void WaylandOutput::setModel(std::string value) { d->model = std::move(value); }
Size WaylandOutput::size() const { return d->size; }
Size WaylandOutput::physicalSize() const { return d->physicalSize; }
void WaylandOutput::setPhysicalSize(Size size) { d->physicalSize = size; }
int WaylandOutput::scale() const { return d->scale; }
void WaylandOutput::setScale(int scale) { d->scale = scale; }
WaylandOutputMode WaylandOutput::currentMode() const { return d->currentMode; }
const std::vector<WaylandOutputMode>& WaylandOutput::modes() const { return d->modes; }
void WaylandOutput::addMode(WaylandOutputMode mode) { d->modes.push_back(mode); }
void WaylandOutput::sendEnter(wl_resource* surfaceResource) const {
    if (wl_resource* output = outputResourceFor(surfaceResource, this))
        wl_surface_send_enter(surfaceResource, output);
}
void WaylandOutput::sendLeave(wl_resource* surfaceResource) const {
    if (wl_resource* output = outputResourceFor(surfaceResource, this))
        wl_surface_send_leave(surfaceResource, output);
}
void WaylandOutput::detachResources() {
    for (wl_resource* resource : d->resources)
        wl_resource_set_user_data(resource, nullptr);
    d->resources.clear();
}
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
    /// The connection ended: delete this client's WaylandClient and drop it from
    /// clients(). libwayland has already unlinked the listener, so it must not
    /// be removed again (the destructor checks the flag).
    static void clientDestroyed(wl_listener* listener, void*) {
        WaylandClient::Impl* impl = wl_container_of(listener, impl, destroyListener);
        impl->watching = false;
        WaylandClient* object = impl->owner;
        WaylandCompositor* compositor = impl->compositor;
        if (compositor) {
            auto& clients = compositor->d->clients;
            clients.erase(std::remove(clients.begin(), clients.end(), object),
                          clients.end());
        }
        delete object;
    }
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
            object->d->compositor = compositor;
            object->watchClient();
        }
        object->addResource(resource);
    }
};

void WaylandClient::watchClient() {
    if (d->watching || !d->client.handle())
        return;
    d->destroyListener.notify = &CompositorGlobal::clientDestroyed;
    wl_client_add_destroy_listener(d->client.handle(), &d->destroyListener);
    d->watching = true;
}

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
    // Pick the buffer backend (drm_egl first, then shm). Failing to load one is
    // not fatal: WaylandSurfaceGrabber::grab() reads wl_shm buffers itself.
    BufferBackends::create(d->display->handle());
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

void WaylandCompositor::removeOutput(WaylandOutput* output) {
    if (!output)
        return;
    auto& outputs = d->outputs;
    outputs.erase(std::remove(outputs.begin(), outputs.end(), output), outputs.end());
    // The surfaces must not remember the pointer: a later output may be
    // allocated at the same address and would then be skipped by
    // announceOutputs().
    for (WaylandSurface* surface : d->surfaces)
        surface->forgetOutput(output);
    // A client keeps its wl_output resource, but the object behind it is going
    // away; clearing the user data makes a late request a no-op instead of a
    // use-after-free. The protocol has no "output removed" event — the client
    // notices the removed global.
    output->detachResources();
    delete output;
}
} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
