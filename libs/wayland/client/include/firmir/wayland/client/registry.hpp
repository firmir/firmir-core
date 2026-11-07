#pragma once

#include <cstdint>
#include <functional>
#include <wayland-client.h>

namespace Firmir {
namespace Wayland {
namespace Client {

class Registry {
public:
    using Global = std::function<void(std::uint32_t, const char*, std::uint32_t)>;
    using GlobalRemove = std::function<void(std::uint32_t)>;
    explicit Registry(wl_registry* registry);
    ~Registry();
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    wl_registry* handle() const { return m_registry; }
    void setGlobalHandler(Global handler) { m_global = std::move(handler); }
    void setGlobalRemoveHandler(GlobalRemove handler) { m_remove = std::move(handler); }
private:
    static void onGlobal(void*, wl_registry*, std::uint32_t, const char*, std::uint32_t);
    static void onGlobalRemove(void*, wl_registry*, std::uint32_t);
    inline static const wl_registry_listener s_listener = {onGlobal, onGlobalRemove};
    wl_registry* m_registry;
    Global m_global;
    GlobalRemove m_remove;
};

} // namespace Client
} // namespace Wayland
} // namespace Firmir
