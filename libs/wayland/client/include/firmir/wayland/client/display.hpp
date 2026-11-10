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
    bool valid() const;
    wl_display* handle() const;
    int fd() const;
    int dispatch();
    int roundtrip();
    int flush();
    int prepareRead();
    void cancelRead();
    int readEvents();
    int dispatchPending();
    /// Last protocol/connection error reported by the compositor (0 = none);
    /// once set, the connection is dead and the dispatch loop should stop.
    int lastError() const;
    std::unique_ptr<Registry> createRegistry();
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Client
} // namespace Wayland
} // namespace Firmir
