#pragma once

#include <firmir/object.hpp>
#include <firmir/gui/events.hpp>
#include <firmir/wayland/server/server.hpp>
#include <wayland-server-protocol.h>

#include <memory>
#include <string>
#include <vector>

namespace Firmir {
namespace Wayland {
namespace Compositor {

using Gui::Size;
class WaylandCompositorExtension;
class WaylandCompositor;
class WaylandSurface;
class WaylandOutput;

class WaylandResource {
public:
    WaylandResource() = default;
    explicit WaylandResource(wl_resource* resource) : m_resource(resource) {}
    wl_resource* handle() const { return m_resource.handle(); }
    Server::Resource serverResource() const { return m_resource; }
    wl_client* client() const;
    std::uint32_t id() const { return m_resource.id(); }
    int version() const { return m_resource.version(); }
private:
    Server::Resource m_resource;
};

class WaylandObject : public Object {
public:
    explicit WaylandObject(Object* parent = nullptr) : Object(parent) {}
    ~WaylandObject() override;
    const std::vector<WaylandCompositorExtension*>& extensions() const { return m_extensions; }
    void addExtension(WaylandCompositorExtension* extension);
private:
    std::vector<WaylandCompositorExtension*> m_extensions;
};

class WaylandCompositorExtension : public WaylandObject {
public:
    explicit WaylandCompositorExtension(Object* parent = nullptr) : WaylandObject(parent) {}
    ~WaylandCompositorExtension() override;
    WaylandCompositor* compositor() const { return m_compositor; }
    virtual bool initialize(WaylandCompositor* compositor);
protected:
    WaylandCompositor* m_compositor = nullptr;
};

template <typename T = WaylandCompositorExtension>
class WaylandCompositorExtensionTemplate : public T {
public:
    using T::T;
    bool initialize(WaylandCompositor* compositor) override {
        this->m_compositor = compositor;
        return this->onInitialize();
    }
    bool onInitialize() { return true; }
};

class WaylandClient : public WaylandObject {
public:
    explicit WaylandClient(wl_client* client = nullptr, Object* parent = nullptr);
    wl_client* handle() const { return m_client.handle(); }
    void credentials(pid_t& pid, uid_t& uid, gid_t& gid) const { m_client.credentials(pid, uid, gid); }
    void flush() const { m_client.flush(); }
    const std::vector<WaylandResource>& resources() const { return m_resources; }
    void addResource(wl_resource* resource) { m_resources.emplace_back(resource); }
private:
    Server::Client m_client;
    std::vector<WaylandResource> m_resources;
};

class WaylandBufferRef {
public:
    WaylandBufferRef() = default;
    explicit WaylandBufferRef(wl_resource* buffer) : m_buffer(buffer) {}
    wl_resource* handle() const { return m_buffer; }
    bool valid() const { return m_buffer != nullptr; }
    bool isShm() const;
    Size size() const;
    int stride() const;
    const void* data() const;
private:
    wl_resource* m_buffer = nullptr;
};

struct WaylandOutputMode {
    int width = 0;
    int height = 0;
    int refreshRate = 60000;
};

class WaylandOutput : public WaylandObject {
public:
    explicit WaylandOutput(Object* parent = nullptr);
    ~WaylandOutput() override;
    bool initialize(WaylandCompositor* compositor);
    std::string manufacturer() const { return m_manufacturer; }
    void setManufacturer(std::string value) { m_manufacturer = std::move(value); }
    std::string model() const { return m_model; }
    void setModel(std::string value) { m_model = std::move(value); }
    Size size() const { return m_size; }
    void setSize(Size size);
    int scale() const { return m_scale; }
    void setScale(int scale) { m_scale = scale; }
    WaylandOutputMode currentMode() const { return m_currentMode; }
    void setCurrentMode(WaylandOutputMode mode);
    const std::vector<WaylandOutputMode>& modes() const { return m_modes; }
    void addMode(WaylandOutputMode mode) { m_modes.push_back(mode); }
private:
    std::string m_manufacturer = "Firmir";
    std::string m_model = "Virtual output";
    Size m_size{1280, 720};
    int m_scale = 1;
    WaylandOutputMode m_currentMode{1280, 720, 60000};
    std::vector<WaylandOutputMode> m_modes;
    wl_global* m_global = nullptr;
};

class WaylandCompositor : public WaylandObject {
public:
    explicit WaylandCompositor(Object* parent = nullptr);
    ~WaylandCompositor() override;
    bool create();
    bool valid() const;
    Server::Display* display() const { return m_display.get(); }
    std::string socketName() const { return m_socketName; }
    std::string addSocketAuto();
    int exec();
    void terminate();
    int dispatch(int timeoutMs = 0);
    WaylandOutput* addOutput(Size size = {1280, 720});
    const std::vector<WaylandOutput*>& outputs() const { return m_outputs; }
    const std::vector<WaylandClient*>& clients() const { return m_clients; }
    const std::vector<WaylandSurface*>& surfaces() const { return m_surfaces; }
private:
    friend struct CompositorGlobal;
    std::unique_ptr<Server::Display> m_display;
    std::string m_socketName;
    std::vector<WaylandOutput*> m_outputs;
    std::vector<WaylandClient*> m_clients;
    std::vector<WaylandSurface*> m_surfaces;
    wl_global* m_compositorGlobal = nullptr;
    bool m_created = false;
};

} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
