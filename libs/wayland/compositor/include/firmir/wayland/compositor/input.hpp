#pragma once

#include <firmir/wayland/compositor/surface.hpp>

namespace Firmir {
namespace Wayland {
namespace Compositor {

class WaylandKeyboard : public WaylandObject {
public:
    explicit WaylandKeyboard(Object* parent = nullptr);
    ~WaylandKeyboard() override;
    /// The most recently bound client resource (legacy convenience).
    WaylandResource resource() const;
    /// Replaces all bound resources with @p resource.
    void setResource(wl_resource* resource);
    /// Registers one more client binding; the stored keymap (if any) is
    /// sent to it immediately.
    void addResource(wl_resource* resource);
    /// Drops a binding (called when the client resource is destroyed).
    void removeResource(wl_resource* resource);
    void setKeymap(int fd, std::uint32_t size);
    /// Keyboard repeat rate (chars/sec) and delay (ms); rate 0 disables
    /// repeat. Replayed to every bound resource (version >= 4) and to all
    /// future bindings.
    void setRepeatInfo(std::int32_t rate, std::int32_t delay);
    void enter(WaylandSurface* surface, const std::vector<std::uint32_t>& keys = {});
    void leave(WaylandSurface* surface);
    void key(std::uint32_t time, std::uint32_t key, bool pressed);
    void modifiers(std::uint32_t depressed, std::uint32_t latched,
                   std::uint32_t locked, std::uint32_t group);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandPointer : public WaylandObject {
public:
    explicit WaylandPointer(Object* parent = nullptr);
    ~WaylandPointer() override;
    WaylandResource resource() const;
    void setResource(wl_resource* resource);
    void addResource(wl_resource* resource);
    void removeResource(wl_resource* resource);
    void enter(WaylandSurface* surface, float x, float y);
    void leave(WaylandSurface* surface);
    void motion(std::uint32_t time, float x, float y);
    void button(std::uint32_t time, std::uint32_t button, bool pressed);
    void axis(std::uint32_t time, std::uint32_t axis, float value);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandTouch : public WaylandObject {
public:
    explicit WaylandTouch(Object* parent = nullptr);
    ~WaylandTouch() override;
    WaylandResource resource() const;
    void setResource(wl_resource* resource);
    void addResource(wl_resource* resource);
    void removeResource(wl_resource* resource);
    void down(WaylandSurface* surface, std::uint32_t time, int id, float x, float y);
    void up(std::uint32_t time, int id);
    void motion(std::uint32_t time, int id, float x, float y);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class WaylandSeat : public WaylandObject {
public:
    explicit WaylandSeat(Object* parent = nullptr);
    ~WaylandSeat() override;
    bool initialize(WaylandCompositor* compositor);
    std::uint32_t capabilities() const;
    void setCapabilities(std::uint32_t capabilities);
    WaylandKeyboard* keyboard() const;
    WaylandPointer* pointer() const;
    WaylandTouch* touch() const;
private:
    class Impl;
    std::unique_ptr<Impl> d;
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
