#pragma once

#include <firmir/wayland/compositor/input.hpp>

namespace Firmir {
namespace Wayland {
namespace Compositor {

class WaylandShellSurface : public WaylandObject {
public:
    explicit WaylandShellSurface(Object* parent = nullptr);
    ~WaylandShellSurface() override;
    WaylandResource resource() const;
    void setResource(wl_resource* resource);
    WaylandSurface* surface() const;
    void setSurface(WaylandSurface* surface);
    std::string title() const;
    void setTitle(std::string title);
    Size size() const;
    void setSize(Size size);
    virtual void sendConfigure(int width, int height);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

template <typename T = WaylandShellSurface>
class WaylandShellSurfaceTemplate : public T {
public:
    using T::T;
    void sendConfigure(int width, int height) override {
        this->setSize({width, height});
        this->onConfigure(width, height);
    }
    void onConfigure(int, int) {}
};

class WaylandShell : public WaylandCompositorExtension {
public:
    explicit WaylandShell(Object* parent = nullptr);
    ~WaylandShell() override;
    virtual std::vector<WaylandShellSurface*> shellSurfaces() const;
    void registerSurface(WaylandShellSurface* surface);
    /// Removes @p surface from shellSurfaces() (called when its resource is
    /// destroyed). Does not delete the object.
    void unregisterSurface(WaylandShellSurface* surface);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandWlShellSurface : public WaylandShellSurfaceTemplate<> {
public:
    explicit WaylandWlShellSurface(Object* parent = nullptr)
        : WaylandShellSurfaceTemplate<>(parent) {}
    void sendConfigure(int width, int height) override;
};

class WaylandWlShell : public WaylandShell {
public:
    explicit WaylandWlShell(Object* parent = nullptr);
    ~WaylandWlShell() override;
    bool initialize(WaylandCompositor* compositor) override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandXdgSurface;

class WaylandXdgPopup : public WaylandShellSurface {
public:
    explicit WaylandXdgPopup(Object* parent = nullptr) : WaylandShellSurface(parent) {}
    void sendConfigure(int width, int height) override;
    void sendPopupDone();
};

class WaylandXdgToplevel : public WaylandShellSurface {
public:
    explicit WaylandXdgToplevel(Object* parent = nullptr);
    ~WaylandXdgToplevel() override;
    void sendConfigure(int width, int height) override;
    void sendClose();
    void setMaximized(bool maximized);
    void setFullscreen(bool fullscreen);
    void setMinimized();
    bool maximized() const;
    bool fullscreen() const;
    /// The xdg_surface this toplevel role belongs to (nullptr for orphaned
    /// objects). Prefer WaylandXdgSurface::sendConfigure() for resize
    /// requests: the xdg protocol requires xdg_surface.configure (with a
    /// serial the client must ack) in addition to xdg_toplevel.configure.
    WaylandXdgSurface* xdgSurface() const;
    void setXdgSurface(WaylandXdgSurface* surface);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandXdgSurface : public WaylandShellSurfaceTemplate<> {
public:
    explicit WaylandXdgSurface(Object* parent = nullptr);
    ~WaylandXdgSurface() override;
    WaylandXdgToplevel* toplevel() const;
    void setToplevel(WaylandXdgToplevel* toplevel);
    WaylandXdgPopup* popup() const;
    void setPopup(WaylandXdgPopup* popup);
    void sendConfigure(int width, int height) override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandXdgShell : public WaylandShell {
public:
    explicit WaylandXdgShell(Object* parent = nullptr);
    ~WaylandXdgShell() override;
    bool initialize(WaylandCompositor* compositor) override;
    void ping(std::uint32_t serial);
private:
    friend struct XdgShellGlobal;

    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandXdgDecorationManagerV1 : public WaylandCompositorExtension {
public:
    enum class Mode { ClientSide = 1, ServerSide = 2 };
    explicit WaylandXdgDecorationManagerV1(Object* parent = nullptr);
    ~WaylandXdgDecorationManagerV1() override;
    bool initialize(WaylandCompositor* compositor) override;
    void setDefaultMode(Mode mode);
    Mode defaultMode() const;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandXdgOutputV1 : public WaylandObject {
public:
    explicit WaylandXdgOutputV1(Object* parent = nullptr);
    ~WaylandXdgOutputV1() override;
    WaylandOutput* output() const;
    void setOutput(WaylandOutput* output);
    WaylandResource resource() const;
    void setResource(wl_resource* resource);
    void sendDescription();
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandXdgOutputManagerV1 : public WaylandCompositorExtension {
public:
    explicit WaylandXdgOutputManagerV1(Object* parent = nullptr);
    ~WaylandXdgOutputManagerV1() override;
    bool initialize(WaylandCompositor* compositor) override;
    WaylandXdgOutputV1* addOutput(WaylandOutput* output);
    const std::vector<WaylandXdgOutputV1*>& outputs() const;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandViewporter : public WaylandCompositorExtension {
public:
    explicit WaylandViewporter(Object* parent = nullptr);
    ~WaylandViewporter() override;
    bool initialize(WaylandCompositor* compositor) override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};
} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
