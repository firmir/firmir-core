#pragma once

#include <wayland-client-core.h>
#include <utility>

namespace Firmir {
namespace Wayland {
namespace Client {

template <typename T>
class Proxy {
public:
    using Destroy = void (*)(T*);
    explicit Proxy(T* handle = nullptr, Destroy destroy = nullptr)
        : m_handle(handle), m_destroy(destroy) {}
    ~Proxy() { destroy(); }
    Proxy(const Proxy&) = delete;
    Proxy& operator=(const Proxy&) = delete;
    Proxy(Proxy&& other) noexcept
        : m_handle(other.m_handle), m_destroy(other.m_destroy) {
        other.m_handle = nullptr;
    }
    Proxy& operator=(Proxy&& other) noexcept {
        if (this != &other) {
            destroy();
            m_handle = other.m_handle;
            m_destroy = other.m_destroy;
            other.m_handle = nullptr;
        }
        return *this;
    }
    T* handle() const { return m_handle; }
    explicit operator bool() const { return m_handle != nullptr; }
    void destroy() {
        if (m_handle && m_destroy) m_destroy(m_handle);
        m_handle = nullptr;
    }
    T* release() { T* result = m_handle; m_handle = nullptr; return result; }
protected:
    T* m_handle;
    Destroy m_destroy;
};

} // namespace Client
} // namespace Wayland
} // namespace Firmir
