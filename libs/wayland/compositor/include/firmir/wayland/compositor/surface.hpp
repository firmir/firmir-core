#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/wayland/compositor/core.hpp>

namespace Firmir {
namespace Wayland {
namespace Compositor {

class WaylandSurfaceRole {
public:
    WaylandSurfaceRole() = default;
    explicit WaylandSurfaceRole(std::string name) : m_name(std::move(name)) {}
    const std::string& name() const { return m_name; }
private:
    std::string m_name;
};

class WaylandSurface : public WaylandObject {
public:
    explicit WaylandSurface(Object* parent = nullptr);
    WaylandResource resource() const { return m_resource; }
    void setResource(wl_resource* resource) { m_resource = WaylandResource(resource); }
    WaylandBufferRef buffer() const { return m_buffer; }
    WaylandBufferRef pendingBuffer() const { return m_pendingBuffer; }
    WaylandSurfaceRole* role() const { return m_role.get(); }
    void setRole(std::unique_ptr<WaylandSurfaceRole> role) { m_role = std::move(role); }
    const std::vector<Gui::Rect>& damage() const { return m_damage; }
    void attach(wl_resource* buffer, int x = 0, int y = 0);
    void addDamage(const Gui::Rect& rect);
    void commit();
    static constexpr auto signalCommitted = "committed";
    static constexpr auto signalDamaged = "damaged";
private:
    WaylandResource m_resource;
    WaylandBufferRef m_buffer;
    WaylandBufferRef m_pendingBuffer;
    std::unique_ptr<WaylandSurfaceRole> m_role;
    std::vector<Gui::Rect> m_damage;
    int m_attachX = 0, m_attachY = 0;
};

class WaylandView : public WaylandObject {
public:
    explicit WaylandView(Object* parent = nullptr) : WaylandObject(parent) {}
    WaylandSurface* surface() const { return m_surface; }
    void setSurface(WaylandSurface* surface) { m_surface = surface; }
    WaylandOutput* output() const { return m_output; }
    void setOutput(WaylandOutput* output) { m_output = output; }
    Gui::Point position() const { return m_position; }
    void setPosition(Gui::Point position) { m_position = position; }
private:
    WaylandSurface* m_surface = nullptr;
    WaylandOutput* m_output = nullptr;
    Gui::Point m_position;
};

class WaylandSurfaceGrabber {
public:
    struct Frame {
        int width = 0, height = 0, stride = 0;
        std::vector<std::uint32_t> pixels;
    };
    Frame grab(WaylandSurface* surface) const;
};

class WaylandGuiItem : public Gui::Item {
public:
    explicit WaylandGuiItem(Object* parent = nullptr);
    WaylandView* view() const { return m_view; }
    void setView(WaylandView* view) { m_view = view; update(); }
protected:
    void paint(Gui::Painter& painter) override;
private:
    WaylandView* m_view = nullptr;
    std::uint64_t m_generation = 0;
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
    explicit WaylandGuiShellSurfaceItem(Object* parent = nullptr) : WaylandGuiItem(parent) {}
    WaylandShellSurface* shellSurface() const { return m_shellSurface; }
    void setShellSurface(WaylandShellSurface* surface) { m_shellSurface = surface; }
private:
    WaylandShellSurface* m_shellSurface = nullptr;
};

} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
