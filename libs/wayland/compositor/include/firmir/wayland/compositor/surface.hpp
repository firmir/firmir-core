#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/wayland/compositor/core.hpp>

namespace Firmir {
namespace Wayland {
namespace Compositor {

class WaylandSurfaceRole {
public:
    WaylandSurfaceRole() : d(std::make_shared<Impl>()) {}
    explicit WaylandSurfaceRole(std::string name) : d(std::make_shared<Impl>()) {
        d->name = std::move(name);
    }
    const std::string& name() const { return d->name; }
private:
    struct Impl {
        std::string name;
    };
    std::shared_ptr<Impl> d;
};

class WaylandSurface : public WaylandObject {
public:
    explicit WaylandSurface(Object* parent = nullptr);
    ~WaylandSurface() override;
    WaylandResource resource() const;
    void setResource(wl_resource* resource);
    WaylandBufferRef buffer() const;
    WaylandBufferRef pendingBuffer() const;
    WaylandSurfaceRole* role() const;
    void setRole(std::unique_ptr<WaylandSurfaceRole> role);
    const std::vector<Gui::Rect>& damage() const;
    void attach(wl_resource* buffer, int x = 0, int y = 0);
    void addDamage(const Gui::Rect& rect);
    void commit();
    static constexpr auto signalCommitted = "committed";
    static constexpr auto signalDamaged = "damaged";
    /// Emitted (no arguments) from the wl_surface resource destroy callback,
    /// BEFORE the object is deleted — observers must detach here (unlike
    /// Object::signalDestroyed, which fires from the destructor when the
    /// buffer is already gone).
    static constexpr auto signalAboutToBeDestroyed = "aboutToBeDestroyed";
private:
    class Impl;
    std::unique_ptr<Impl> d;
    // The grabber locks the surface's buffer mutex for the whole pixel copy.
    friend class WaylandSurfaceGrabber;
};

class WaylandView : public WaylandObject {
public:
    explicit WaylandView(Object* parent = nullptr);
    ~WaylandView() override;
    WaylandSurface* surface() const;
    void setSurface(WaylandSurface* surface);
    WaylandOutput* output() const;
    void setOutput(WaylandOutput* output);
    Gui::Point position() const;
    void setPosition(Gui::Point position);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandSurfaceGrabber {
public:
    struct Frame {
        int width = 0, height = 0, stride = 0;
        std::vector<std::uint32_t> pixels;
    };
    Frame grab(WaylandSurface* surface) const;
};

class WaylandSeat;

class WaylandGuiItem : public Gui::Item {
public:
    explicit WaylandGuiItem(Object* parent = nullptr);
    ~WaylandGuiItem() override;
    WaylandView* view() const;
    void setView(WaylandView* view);
    /// Seat that receives the pointer/keyboard input collected by this item.
    /// Without one the item is paint-only.
    WaylandSeat* seat() const;
    void setSeat(WaylandSeat* seat);
    /// Cuts the item off from its (possibly dying) surface: the paint stops
    /// and no further input is forwarded. Takes the scene lock so an
    /// in-flight render never observes a stale surface.
    void detachSurface();
    /// Emitted (no arguments) on a pointer press and when the item gains
    /// focus — the embedding compositor raises/activates its window here.
    static constexpr auto signalActivated = "activated";
protected:
    void paint(Gui::Painter& painter) override;
    /// Pointer input -> wl_pointer of the view's surface (enter/motion/
    /// button/axis). Item coordinates are scaled to surface coordinates, so
    /// a stretched item (maximized/fullscreen) stays correct, and a pressed
    /// button keeps the implicit grab until its release.
    void mouseEvent(Gui::MouseEvent& event) override;
    /// Keyboard input -> wl_keyboard of the focused surface.
    void keyEvent(Gui::KeyEvent& event) override;
    /// Touch input -> wl_touch of the view's surface. Only the contacts that
    /// began on this item are forwarded; they keep sending motion/up after
    /// the finger slides outside (a touch has no leave event).
    void touchEvent(Gui::TouchEvent& event) override;
    void focusInEvent() override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandShellSurface;

class WaylandGuiShellIntegration : public WaylandCompositorExtension {
public:
    explicit WaylandGuiShellIntegration(Object* parent = nullptr)
        : WaylandCompositorExtension(parent) {}
    WaylandGuiItem* createItem(WaylandView* view, Object* parent = nullptr);
};

class WaylandGuiShellSurfaceItem : public WaylandGuiItem {
public:
    explicit WaylandGuiShellSurfaceItem(Object* parent = nullptr);
    ~WaylandGuiShellSurfaceItem() override;
    WaylandShellSurface* shellSurface() const;
    void setShellSurface(WaylandShellSurface* surface);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
