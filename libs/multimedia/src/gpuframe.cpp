#include <firmir/multimedia/gpuframe.hpp>

#include <chrono>
#include <cstdlib>

namespace Firmir {
namespace Multimedia {

class VideoFramePool::Impl {
public:
    AcquireFn acquireFn;
    void** slots = nullptr;
    int capacity = 0;
    int free = 0;
    mutable std::mutex mutex;
    std::atomic<std::uint64_t> acquired{0};
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<std::uint64_t> waitUs{0};
};

VideoFramePool::VideoFramePool(int capacity, AcquireFn acquireFn)
    : d(std::make_unique<Impl>()) {
    d->acquireFn = std::move(acquireFn);
    d->slots = new void*[static_cast<size_t>(capacity)];
    d->capacity = capacity;
    d->free = capacity;
    for (int i = 0; i < capacity; ++i)
        d->slots[i] =
            reinterpret_cast<void*>(static_cast<std::uintptr_t>(i) + 1);
}

VideoFramePool::~VideoFramePool() {
    delete[] d->slots;
}

std::shared_ptr<GpuFrameHandle> VideoFramePool::acquire() {
    const auto started = std::chrono::steady_clock::now();
    void* slot = nullptr;
    {
        // Short critical section: pointer moves only, never held over
        // allocation or decode work.
        std::lock_guard<std::mutex> lock(d->mutex);
        if (d->free > 0)
            slot = d->slots[--d->free];
    }
    d->waitUs += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    if (!slot) {
        ++d->dropped;
        return nullptr;
    }
    ++d->acquired;
    auto inner = d->acquireFn(slot);
    if (!inner) { // backend allocation failed: return the slot
        std::lock_guard<std::mutex> lock(d->mutex);
        d->slots[d->free++] = slot;
        return nullptr;
    }
    // Re-wrap with the pool-returning deleter (keeps the handle's own type).
    return std::shared_ptr<GpuFrameHandle>(
        inner.get(), [this, slot, inner](GpuFrameHandle*) mutable {
            inner.reset(); // destroys the handle (returns native surface)
            std::lock_guard<std::mutex> lock(d->mutex);
            d->slots[d->free++] = slot;
        });
}

int VideoFramePool::capacity() const {
    return d->capacity;
}

int VideoFramePool::activeCount() const {
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->capacity - d->free;
}

std::uint64_t VideoFramePool::acquireCount() const {
    return d->acquired.load();
}

std::uint64_t VideoFramePool::droppedAcquires() const {
    return d->dropped.load();
}

std::uint64_t VideoFramePool::totalWaitUs() const {
    return d->waitUs.load();
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
