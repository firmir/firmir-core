#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/wayland/compositor/core.hpp>

#include <cstdint>

namespace Firmir {

namespace Render {
class RenderDevice;
class RenderTexture;
} // namespace Render

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
    /// Announces the surface on the compositor's outputs (wl_surface.enter),
    /// once per output. commit() does it when the client first attaches a
    /// buffer: from then on the window is shown and the client needs to know
    /// its screen (scale, refresh rate).
    void announceOutputs();
    /// Bumped on every commit that attaches a buffer. Lets a client-side item
    /// skip re-copying (and re-uploading) pixels the client did not change.
    std::uint64_t commitSerial() const;
    static constexpr auto signalCommitted = "committed";
    static constexpr auto signalDamaged = "damaged";
    /// Emitted (no arguments) from the wl_surface resource destroy callback,
    /// BEFORE the object is deleted — observers must detach here (unlike
    /// Object::signalDestroyed, which fires from the destructor when the
    /// buffer is already gone).
    static constexpr auto signalAboutToBeDestroyed = "aboutToBeDestroyed";
private:
    friend class WaylandCompositor;

    /// Drops @p output from the set of outputs this surface already entered
    /// (called when an output is removed, so a new one reusing the address
    /// still gets its wl_surface.enter).
    void forgetOutput(WaylandOutput* output);

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
        /// R,G,B,A bytes, top row first — the layout Painter::drawRgba() takes.
        /// The buffer backends hand out 0xAARRGGBB values; grab() converts them.
        std::vector<std::uint32_t> pixels;
    };
    Frame grab(WaylandSurface* surface) const;
    /**
     * @brief Imports the current buffer of @surface on @p device as a texture.
     *
     * A client that draws with the GPU hands its frame over as a dma-buf; the
     * render device samples it directly, so the frame never passes through the
     * CPU (Painter::drawTexture() draws the result). Returns nullptr for a
     * wl_shm buffer, for a backend without a GPU view and for a device that
     * cannot import dma-bufs — grab() then has the pixels.
     */
    std::unique_ptr<Render::RenderTexture> importTexture(WaylandSurface* surface,
                                                         Render::RenderDevice* device) const;
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
    /// The item must still be alive when this runs: own the connection next to
    /// the item (the item as the signal receiver, or a handle dropped before
    /// the scene goes away) — a wl_surface destroy can arrive long after the
    /// item is gone, and cannot be answered then.
    void detachSurface();
    /**
     * @brief Moves the zero-copy texture of the painted frame out of the item.
     *
     * A compositor keeps it as the last picture of a window whose surface is
     * going away: the texture already holds the frame, so nothing is read back
     * from the client buffer (a fullscreen frame is ~10 MB of memory traffic).
     * Empty when the client draws through wl_shm — take the frame instead.
     */
    std::unique_ptr<Render::RenderTexture> takeTexture();
    /**
     * @brief Moves the frame copied out of the surface (wl_shm fallback).
     *
     * Same purpose as takeTexture(): the picture the item already holds is
     * reused instead of copying the buffer again while the client dies.
     */
    WaylandSurfaceGrabber::Frame takeFrame();
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
