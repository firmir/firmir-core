#pragma once

#include <wayland-client-core.h>
#include <memory>
#include <utility>

namespace Firmir {
namespace Wayland {
namespace Client {

template <typename T>
class Proxy {
public:
    using Destroy = void (*)(T*);
    explicit Proxy(T* handle = nullptr, Destroy destroy = nullptr)
        : d(std::make_unique<Impl>()) {
        d->handle = handle;
        d->destroy = destroy;
    }
    ~Proxy() { destroy(); }
    Proxy(const Proxy&) = delete;
    Proxy& operator=(const Proxy&) = delete;
    Proxy(Proxy&& other) noexcept : d(std::move(other.d)) {
        other.d = std::make_unique<Impl>();
    }
    Proxy& operator=(Proxy&& other) noexcept {
        if (this != &other) {
            destroy();
            d = std::move(other.d);
            other.d = std::make_unique<Impl>();
        }
        return *this;
    }
    T* handle() const { return d->handle; }
    explicit operator bool() const { return d->handle != nullptr; }
    void destroy() {
        if (d->handle && d->destroy) d->destroy(d->handle);
        d->handle = nullptr;
    }
    T* release() { T* result = d->handle; d->handle = nullptr; return result; }
private:
    // Template classes cannot hide Impl in a .cpp; the pointer indirection
    // is kept anyway for uniformity with the rest of the framework.
    struct Impl {
        T* handle = nullptr;
        Destroy destroy = nullptr;
    };
    std::unique_ptr<Impl> d;
};

} // namespace Client
} // namespace Wayland
} // namespace Firmir
