#pragma once

#include <firmir/wayland/compositor/input.hpp>

namespace Firmir {
namespace Wayland {
namespace Compositor {

class WaylandShellSurface : public WaylandObject {
public:
    explicit WaylandShellSurface(Object* parent = nullptr) : WaylandObject(parent) {}
    ~WaylandShellSurface() override;
    WaylandResource resource() const { return m_resource; }
    void setResource(wl_resource* resource) { m_resource = WaylandResource(resource); }
    WaylandSurface* surface() const { return m_surface; }
    void setSurface(WaylandSurface* surface) { m_surface = surface; }
    std::string title() const { return m_title; }
    void setTitle(std::string title) { m_title = std::move(title); }
    Size size() const { return m_size; }
    void setSize(Size size) { m_size = size; }
    virtual void sendConfigure(int width, int height);
protected:
    WaylandResource m_resource;
    WaylandSurface* m_surface = nullptr;
    std::string m_title;
    Size m_size;
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
    explicit WaylandShell(Object* parent = nullptr) : WaylandCompositorExtension(parent) {}
    virtual std::vector<WaylandShellSurface*> shellSurfaces() const { return m_surfaces; }
    void registerSurface(WaylandShellSurface* surface) { m_surfaces.push_back(surface); }
protected:
    std::vector<WaylandShellSurface*> m_surfaces;
};

class WaylandWlShellSurface : public WaylandShellSurfaceTemplate<> {
public:
    explicit WaylandWlShellSurface(Object* parent = nullptr)
        : WaylandShellSurfaceTemplate<>(parent) {}
    void sendConfigure(int width, int height) override;
};

class WaylandWlShell : public WaylandShell {
public:
    explicit WaylandWlShell(Object* parent = nullptr) : WaylandShell(parent) {}
    ~WaylandWlShell() override;
    bool initialize(WaylandCompositor* compositor) override;
private:
    wl_global* m_global = nullptr;
};

class WaylandXdgPopup : public WaylandShellSurface {
public:
    explicit WaylandXdgPopup(Object* parent = nullptr) : WaylandShellSurface(parent) {}
    void sendConfigure(int width, int height) override;
    void sendPopupDone();
};

class WaylandXdgToplevel : public WaylandShellSurface {
public:
    explicit WaylandXdgToplevel(Object* parent = nullptr) : WaylandShellSurface(parent) {}
    void sendConfigure(int width, int height) override;
    void sendClose();
    void setMaximized(bool maximized);
    void setFullscreen(bool fullscreen);
    void setMinimized();
    bool maximized() const { return m_maximized; }
    bool fullscreen() const { return m_fullscreen; }
private:
    bool m_maximized = false;
    bool m_fullscreen = false;
};

class WaylandXdgSurface : public WaylandShellSurfaceTemplate<> {
public:
    explicit WaylandXdgSurface(Object* parent = nullptr)
        : WaylandShellSurfaceTemplate<>(parent) {}
    WaylandXdgToplevel* toplevel() const { return m_toplevel; }
    void setToplevel(WaylandXdgToplevel* toplevel) { m_toplevel = toplevel; }
    WaylandXdgPopup* popup() const { return m_popup; }
    void setPopup(WaylandXdgPopup* popup) { m_popup = popup; }
    void sendConfigure(int width, int height) override;
private:
    WaylandXdgToplevel* m_toplevel = nullptr;
    WaylandXdgPopup* m_popup = nullptr;
};

class WaylandXdgShell : public WaylandShell {
public:
    explicit WaylandXdgShell(Object* parent = nullptr) : WaylandShell(parent) {}
    ~WaylandXdgShell() override;
    bool initialize(WaylandCompositor* compositor) override;
    void ping(std::uint32_t serial);
private:
    friend struct XdgShellGlobal;
    std::vector<wl_resource*> m_resources;
    wl_global* m_global = nullptr;
};

class WaylandXdgDecorationManagerV1 : public WaylandCompositorExtension {
public:
    enum class Mode { ClientSide = 1, ServerSide = 2 };
    explicit WaylandXdgDecorationManagerV1(Object* parent = nullptr)
        : WaylandCompositorExtension(parent) {}
    ~WaylandXdgDecorationManagerV1() override;
    bool initialize(WaylandCompositor* compositor) override;
    void setDefaultMode(Mode mode) { m_defaultMode = mode; }
    Mode defaultMode() const { return m_defaultMode; }
private:
    Mode m_defaultMode = Mode::ClientSide;
    wl_global* m_global = nullptr;
};

class WaylandXdgOutputV1 : public WaylandObject {
public:
    explicit WaylandXdgOutputV1(Object* parent = nullptr) : WaylandObject(parent) {}
    WaylandOutput* output() const { return m_output; }
    void setOutput(WaylandOutput* output) { m_output = output; }
    WaylandResource resource() const { return m_resource; }
    void setResource(wl_resource* resource) { m_resource = WaylandResource(resource); }
    void sendDescription();
private:
    WaylandOutput* m_output = nullptr;
    WaylandResource m_resource;
};

class WaylandXdgOutputManagerV1 : public WaylandCompositorExtension {
public:
    explicit WaylandXdgOutputManagerV1(Object* parent = nullptr)
        : WaylandCompositorExtension(parent) {}
    ~WaylandXdgOutputManagerV1() override;
    bool initialize(WaylandCompositor* compositor) override;
    WaylandXdgOutputV1* addOutput(WaylandOutput* output);
    const std::vector<WaylandXdgOutputV1*>& outputs() const { return m_outputs; }
private:
    std::vector<WaylandXdgOutputV1*> m_outputs;
    wl_global* m_global = nullptr;
};

class WaylandViewporter : public WaylandCompositorExtension {
public:
    explicit WaylandViewporter(Object* parent = nullptr)
        : WaylandCompositorExtension(parent) {}
    ~WaylandViewporter() override;
    bool initialize(WaylandCompositor* compositor) override;
private:
    wl_global* m_global = nullptr;
};
} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
