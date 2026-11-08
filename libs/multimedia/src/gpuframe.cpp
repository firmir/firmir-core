#include <firmir/multimedia/gpuframe.hpp>

#include <chrono>
#include <cstdlib>

namespace Firmir {
namespace Multimedia {

VideoFramePool::VideoFramePool(int capacity, AcquireFn acquireFn)
    : m_acquireFn(std::move(acquireFn)),
      m_slots(new void*[static_cast<size_t>(capacity)]), m_capacity(capacity),
      m_free(capacity) {
    for (int i = 0; i < capacity; ++i)
        m_slots[i] =
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(i) + 1);
}

VideoFramePool::~VideoFramePool() {
    delete[] m_slots;
}

std::shared_ptr<GpuFrameHandle> VideoFramePool::acquire() {
    const auto started = std::chrono::steady_clock::now();
    void* slot = nullptr;
    {
        // Short critical section: pointer moves only, never held over
        // allocation or decode work.
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_free > 0)
            slot = m_slots[--m_free];
    }
    m_waitUs += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    if (!slot) {
        ++m_dropped;
        return nullptr;
    }
    ++m_acquired;
    auto inner = m_acquireFn(slot);
    if (!inner) { // backend allocation failed: return the slot
        std::lock_guard<std::mutex> lock(m_mutex);
        m_slots[m_free++] = slot;
        return nullptr;
    }
    // Re-wrap with the pool-returning deleter (keeps the handle's own type).
    return std::shared_ptr<GpuFrameHandle>(
        inner.get(), [this, slot, inner](GpuFrameHandle*) mutable {
            inner.reset(); // destroys the handle (returns native surface)
            std::lock_guard<std::mutex> lock(m_mutex);
            m_slots[m_free++] = slot;
        });
}

int VideoFramePool::capacity() const {
    return m_capacity;
}

int VideoFramePool::activeCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_capacity - m_free;
}

std::uint64_t VideoFramePool::acquireCount() const {
    return m_acquired.load();
}

std::uint64_t VideoFramePool::droppedAcquires() const {
    return m_dropped.load();
}

std::uint64_t VideoFramePool::totalWaitUs() const {
    return m_waitUs.load();
}

bool zeroCopyEnabled() {
    static const int enabled = [] {
        const char* env = std::getenv("FIRMIR_MM_ZERO_COPY");
        return !(env && (env[0] == '0' || env[0] == 'n' || env[0] == 'N'));
    }();
    return enabled != 0;
}

} // namespace Multimedia
} // namespace Firmir
