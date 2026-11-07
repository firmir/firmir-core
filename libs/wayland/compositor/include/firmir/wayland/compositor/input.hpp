#pragma once

#include <firmir/wayland/compositor/surface.hpp>

namespace Firmir {
namespace Wayland {
namespace Compositor {

class WaylandKeyboard : public WaylandObject {
public:
    explicit WaylandKeyboard(Object* parent = nullptr) : WaylandObject(parent) {}
    WaylandResource resource() const { return m_resource; }
    void setResource(wl_resource* resource) { m_resource = WaylandResource(resource); }
    void setKeymap(int fd, std::uint32_t size);
    void enter(WaylandSurface* surface, const std::vector<std::uint32_t>& keys = {});
    void leave(WaylandSurface* surface);
    void key(std::uint32_t time, std::uint32_t key, bool pressed);
    void modifiers(std::uint32_t depressed, std::uint32_t latched,
                   std::uint32_t locked, std::uint32_t group);
private:
    WaylandResource m_resource;
};

class WaylandPointer : public WaylandObject {
public:
    explicit WaylandPointer(Object* parent = nullptr) : WaylandObject(parent) {}
    WaylandResource resource() const { return m_resource; }
    void setResource(wl_resource* resource) { m_resource = WaylandResource(resource); }
    void enter(WaylandSurface* surface, float x, float y);
    void leave(WaylandSurface* surface);
    void motion(std::uint32_t time, float x, float y);
    void button(std::uint32_t time, std::uint32_t button, bool pressed);
    void axis(std::uint32_t time, std::uint32_t axis, float value);
private:
    WaylandResource m_resource;
};

class WaylandTouch : public WaylandObject {
public:
    explicit WaylandTouch(Object* parent = nullptr) : WaylandObject(parent) {}
    WaylandResource resource() const { return m_resource; }
    void setResource(wl_resource* resource) { m_resource = WaylandResource(resource); }
    void down(WaylandSurface* surface, std::uint32_t time, int id, float x, float y);
    void up(std::uint32_t time, int id);
    void motion(std::uint32_t time, int id, float x, float y);
private:
    WaylandResource m_resource;
};

class WaylandSeat : public WaylandObject {
public:
    explicit WaylandSeat(Object* parent = nullptr);
    ~WaylandSeat() override;
    bool initialize(WaylandCompositor* compositor);
    std::uint32_t capabilities() const { return m_capabilities; }
    void setCapabilities(std::uint32_t capabilities);
    WaylandKeyboard* keyboard() const { return m_keyboard.get(); }
    WaylandPointer* pointer() const { return m_pointer.get(); }
    WaylandTouch* touch() const { return m_touch.get(); }
private:
    std::uint32_t m_capabilities = 0;
    wl_global* m_global = nullptr;
    std::unique_ptr<WaylandKeyboard> m_keyboard;
    std::unique_ptr<WaylandPointer> m_pointer;
    std::unique_ptr<WaylandTouch> m_touch;
};

class WaylandTextInputManager : public WaylandCompositorExtension {
public:
    explicit WaylandTextInputManager(Object* parent = nullptr)
        : WaylandCompositorExtension(parent) {}
    void commitString(WaylandSurface* surface, const std::string& text);
    void setPreeditString(WaylandSurface* surface, const std::string& text);
};

class WaylandQtTextInputMethodManager : public WaylandTextInputManager {
public:
    explicit WaylandQtTextInputMethodManager(Object* parent = nullptr)
        : WaylandTextInputManager(parent) {}
};

} // namespace Compositor
} // namespace Wayland
} // namespace Firmir
