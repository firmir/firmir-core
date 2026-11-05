#pragma once

#include <memory>
#include <wayland-client-core.h>

namespace Firmir {
namespace Wayland {
namespace Client {

class Registry;

/// Owning wl_display connection with the canonical prepare/read dispatch API.
class Display {
public:
    explicit Display(const char* name = nullptr);
    ~Display();
    Display(const Display&) = delete;
    Display& operator=(const Display&) = delete;
    bool valid() const { return m_display != nullptr; }
    wl_display* handle() const { return m_display; }
    int fd() const;
    int dispatch();
    int roundtrip();
    int flush();
    int prepareRead();
    void cancelRead();
    int readEvents();
    int dispatchPending();
    std::unique_ptr<Registry> createRegistry();
private:
    wl_display* m_display;
};

} // namespace Client
} // namespace Wayland
} // namespace Firmir
