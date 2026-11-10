#pragma once

#include <cstdint>
#include <functional>
#include <memory>
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
    wl_registry* handle() const;
    void setGlobalHandler(Global handler);
    void setGlobalRemoveHandler(GlobalRemove handler);
private:
    static void onGlobal(void*, wl_registry*, std::uint32_t, const char*, std::uint32_t);
    static void onGlobalRemove(void*, wl_registry*, std::uint32_t);
    inline static const wl_registry_listener s_listener = {onGlobal, onGlobalRemove};

    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Client
} // namespace Wayland
} // namespace Firmir
