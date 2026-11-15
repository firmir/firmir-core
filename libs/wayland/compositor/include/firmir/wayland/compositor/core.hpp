#pragma once

#include <firmir/object.hpp>
#include <firmir/gui/events.hpp>
#include <firmir/wayland/server/server.hpp>
#include <wayland-server-protocol.h>

#include <memory>
#include <shared_mutex>
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
    friend struct CompositorGlobal;

    /// Starts watching the connection: when the client goes away (it exits or
    /// the compositor disconnects it) the object deletes itself and leaves
    /// WaylandCompositor::clients(). Called when the client first binds
    /// wl_compositor, i.e. once per connection.
    void watchClient();

    class Impl;
    std::unique_ptr<Impl> d;
};

/// Copyable value wrapper around a wl_buffer resource (see WaylandResource).
class WaylandBufferRef {
public:
    WaylandBufferRef() : d(std::make_shared<Impl>()) {}
    explicit WaylandBufferRef(wl_resource* buffer);
    wl_resource* handle() const { return d->buffer; }
    bool valid() const { return d->buffer != nullptr; }
    bool isShm() const;
    Size size() const;
    int stride() const;
    const void* data() const;
    /**
     * @brief Keeps the pixels of the buffer readable for the guard's lifetime.
     *
     * The memory of a wl_shm buffer is the client's shared mapping: it is
     * unmapped as soon as the client destroys the wl_buffer, which a
     * disconnecting client does on the wayland thread — possibly while the
     * render thread is still copying the last frame out of it. A reader holds
     * this lock across the copy and the destruction waits for it, so a copy
     * never runs against a mapping that is going away.
     *
     * Hold one per copy, not one nested inside another: the lock is shared, but
     * a thread that already holds it blocks on a second acquisition as soon as a
     * destruction is waiting.
     */
    std::shared_lock<std::shared_mutex> readLock() const;
private:
    struct Impl {
        Impl() = default;
        ~Impl();
        Impl(const Impl&) = delete;
        Impl& operator=(const Impl&) = delete;
        /// Clears the pointer when the client destroys the wl_buffer. The
        /// resource belongs to the client, so it can disappear while a
        /// committed buffer is still referenced: a disconnecting client
        /// destroys its buffers before the surface they were committed to, and
        /// the compositor sees the surface destroy callback afterwards.
        static void destroyed(wl_listener* listener, void* data);
        wl_resource* buffer = nullptr;
        wl_listener destroyListener{};
        bool watching = false;
        /// See readLock(): readers share it, destroyed() takes it exclusively
        /// before the mapping is unmapped (libwayland emits the destroy
        /// listeners before the interface destroy runs, and that is what
        /// unmaps the pool).
        std::shared_mutex accessMutex;
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
    /// Physical panel size in millimetres, reported to clients through
    /// wl_output.geometry so their millimetre-based layout matches the panel;
    /// {0, 0} (the default) means "unknown" and clients use their own default
    /// density.
    Size physicalSize() const;
    void setPhysicalSize(Size size);
    int scale() const;
    void setScale(int scale);
    WaylandOutputMode currentMode() const;
    void setCurrentMode(WaylandOutputMode mode);
    const std::vector<WaylandOutputMode>& modes() const;
    void addMode(WaylandOutputMode mode);
    /// wl_surface.enter / wl_surface.leave for a client's surface, sent only
    /// if that client bound this output. A client learns from enter which
    /// screen, scale and refresh rate its surface landed on; a surface that
    /// never enters an output is not mapped by some toolkits, which then
    /// leaves the window blank.
    void sendEnter(wl_resource* surfaceResource) const;
    void sendLeave(wl_resource* surfaceResource) const;

    /**
     * @brief Clears the user data of every client binding and forgets them.
     *
     * Called by WaylandCompositor::removeOutput() right before the object is
     * deleted: the client keeps its wl_output resource, but a late request finds
     * no object behind it instead of a freed pointer.
     */
    void detachResources();
private:
    friend struct OutputBindings;

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
    /**
     * @brief Destroys @p output and drops it from outputs().
     *
     * The wl_output global goes away (so no client binds it any more and no
     * wl_surface.enter is announced for it) and the object is deleted. Clients
     * that already bound the output keep their resource, but its user data is
     * cleared, so a late request cannot reach the freed object; the protocol
     * has no "output removed" event, so a client learns about the unplug from
     * the removed global.
     *
     * Send WaylandOutput::sendLeave() for the surfaces that were on the output
     * before calling this.
     */
    void removeOutput(WaylandOutput* output);
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
