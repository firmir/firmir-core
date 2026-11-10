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

/// Copyable value wrapper around a server resource; copies share the Impl
/// (same pattern as Server::Resource).
class WaylandResource {
public:
    WaylandResource() : d(std::make_shared<Impl>()) {}
    explicit WaylandResource(wl_resource* resource) : d(std::make_shared<Impl>()) {
        d->resource = Server::Resource(resource);
    }
    wl_resource* handle() const { return d->resource.handle(); }
    Server::Resource serverResource() const { return d->resource; }
    wl_client* client() const;
    std::uint32_t id() const { return d->resource.id(); }
    int version() const { return d->resource.version(); }
private:
    struct Impl {
        Server::Resource resource;
    };
    std::shared_ptr<Impl> d;
};

class WaylandObject : public Object {
public:
    explicit WaylandObject(Object* parent = nullptr);
    ~WaylandObject() override;
    const std::vector<WaylandCompositorExtension*>& extensions() const;
    void addExtension(WaylandCompositorExtension* extension);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandCompositorExtension : public WaylandObject {
public:
    explicit WaylandCompositorExtension(Object* parent = nullptr);
    ~WaylandCompositorExtension() override;
    WaylandCompositor* compositor() const;
    virtual bool initialize(WaylandCompositor* compositor);
protected:
    void setCompositor(WaylandCompositor* compositor);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

template <typename T = WaylandCompositorExtension>
class WaylandCompositorExtensionTemplate : public T {
public:
    using T::T;
    bool initialize(WaylandCompositor* compositor) override {
        this->setCompositor(compositor);
        return this->onInitialize();
    }
    bool onInitialize() { return true; }
};

class WaylandClient : public WaylandObject {
public:
    explicit WaylandClient(wl_client* client = nullptr, Object* parent = nullptr);
    ~WaylandClient() override;
    wl_client* handle() const;
    void credentials(pid_t& pid, uid_t& uid, gid_t& gid) const;
    void flush() const;
    const std::vector<WaylandResource>& resources() const;
    void addResource(wl_resource* resource);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

/// Copyable value wrapper around a wl_buffer resource (see WaylandResource).
class WaylandBufferRef {
public:
    WaylandBufferRef() : d(std::make_shared<Impl>()) {}
    explicit WaylandBufferRef(wl_resource* buffer) : d(std::make_shared<Impl>()) {
        d->buffer = buffer;
    }
    wl_resource* handle() const { return d->buffer; }
    bool valid() const { return d->buffer != nullptr; }
    bool isShm() const;
    Size size() const;
    int stride() const;
    const void* data() const;
private:
    struct Impl {
        wl_resource* buffer = nullptr;
    };
    std::shared_ptr<Impl> d;
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
    std::string manufacturer() const;
    void setManufacturer(std::string value);
    std::string model() const;
    void setModel(std::string value);
    Size size() const;
    void setSize(Size size);
    int scale() const;
    void setScale(int scale);
    WaylandOutputMode currentMode() const;
    void setCurrentMode(WaylandOutputMode mode);
    const std::vector<WaylandOutputMode>& modes() const;
    void addMode(WaylandOutputMode mode);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandCompositor : public WaylandObject {
public:
    explicit WaylandCompositor(Object* parent = nullptr);
    ~WaylandCompositor() override;
    bool create();
    bool valid() const;
    Server::Display* display() const;
    std::string socketName() const;
    std::string addSocketAuto();
    int exec();
    void terminate();
    int dispatch(int timeoutMs = 0);
    WaylandOutput* addOutput(Size size = {1280, 720});
    const std::vector<WaylandOutput*>& outputs() const;
    const std::vector<WaylandClient*>& clients() const;
    const std::vector<WaylandSurface*>& surfaces() const;
private:
    friend struct CompositorGlobal;

    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
