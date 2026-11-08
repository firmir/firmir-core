#pragma once

// Zero-copy GPU video frame plumbing for firmir-multimedia:
//  - GpuFrameHandle: opaque handle to a GPU-side decoded frame (dma-buf);
//  - VideoFramePool: bounded pool with the explicit lifecycle
//    Acquire -> Decode -> Render -> Release and wait/drop accounting;
//  - VideoMetrics: observability counters for the chosen pipeline path.
//
// The CPU pixel path (VideoFrame::rgba) is unchanged; a frame may carry a
// GpuFrameHandle instead, with a lazy CPU readback for the fallback path.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

namespace Firmir {
namespace Multimedia {

/// Pixel format of a decoded frame.
enum class VideoPixelFormat {
    Unknown,
    Rgba8, ///< 8-bit R,G,B,A per pixel (CPU or single-plane dma-buf).
    Nv12,  ///< 8-bit semi-planar YUV 4:2:0 (two dma-buf planes).
    P010,  ///< 10-bit semi-planar YUV 4:2:0 in 16-bit containers (HDR).
    P016,  ///< 16-bit semi-planar YUV 4:2:0.
};
/// Color conversion matrix signalled by the container/stream.
enum class VideoColorSpace { Unknown, Bt601, Bt709, Bt2020 };
/// Sample range: limited (TV) or full (PC).
enum class VideoColorRange { Unknown, Limited, Full };
/// The pipeline path in effect for a source/output pair.
enum class VideoPath {
    Unknown,
    ZeroCopy,    ///< decoded frames stay in GPU memory (dma-buf -> renderer)
    CpuFallback, ///< frames are read back to system memory once
};

/**
 * @brief Opaque handle to a GPU-resident decoded video frame.
 *
 * Never exposes pixels. The handle borrows its resources from a
 * VideoFramePool (or a backend pool); destroying the handle returns the
 * surface to the pool. Thread-safe to hold across threads; all access is
 * const after publication.
 */
class GpuFrameHandle {
public:
    virtual ~GpuFrameHandle() = default;

    /// Opaque backend tag: how the renderer can import this frame.
    enum class Backend { DmaBuf };
    virtual Backend backend() const = 0;

    // ---- dma-buf view (backend() == DmaBuf) ----
    /// Number of dma-buf planes (1 for RGB, 2 for NV12/P010/P016).
    virtual int planeCount() const = 0;
    /// Borrowed dma-buf fd of plane i; valid while the handle is alive.
    virtual int planeFd(int plane) const = 0;
    virtual std::uint32_t planeOffset(int plane) const = 0;
    virtual std::uint32_t planeStride(int plane) const = 0;
    /// DRM fourcc (Render::Fourcc::*) and DRM format modifier.
    virtual std::uint32_t drmFormat() const = 0;
    virtual std::uint64_t drmModifier() const = 0;
    /// Stable identifier of the underlying pool surface; renderers key their
    /// imported-texture cache on it (no per-frame texture allocation).
    virtual std::uintptr_t poolKey() const = 0;

    // ---- metadata ----
    int width = 0;
    int height = 0;
    VideoPixelFormat format = VideoPixelFormat::Unknown;
    VideoColorSpace colorSpace = VideoColorSpace::Unknown;
    VideoColorRange colorRange = VideoColorRange::Unknown;
    double pts = 0.0;     ///< presentation timestamp, seconds
    int quarterTurns = 0; ///< display orientation, clockwise 90-degree steps
};

/**
 * @brief Bounded pool of GPU frame surfaces with an explicit lifecycle.
 *
 * Acquire -> Decode -> Render -> Release. Native surface allocation is
 * delegated to the acquire callback (VAAPI/ffmpeg hwframes, GstBufferPool,
 * ...); the pool only bounds in-flight frames and accounts waits and drops.
 * acquire() never blocks: on an empty pool it returns nullptr and bumps
 * droppedAcquires() — the decoder must drop the frame instead of stalling
 * (no blocking waits in the hot path).
 */
class VideoFramePool {
public:
    /// The callback receives the pool slot token and must return a handle
    /// whose surface is exclusively owned until the handle dies.
    using AcquireFn =
        std::function<std::shared_ptr<GpuFrameHandle>(void* slot)>;

    VideoFramePool(int capacity, AcquireFn acquireFn);
    ~VideoFramePool();

    VideoFramePool(const VideoFramePool&) = delete;
    VideoFramePool& operator=(const VideoFramePool&) = delete;

    /// Takes a surface; nullptr (and a drop tick) when the pool is empty.
    /// The handle returns itself to the pool on destruction.
    std::shared_ptr<GpuFrameHandle> acquire();

    int capacity() const;
    /// Surfaces currently checked out (decode/render in flight).
    int activeCount() const;
    std::uint64_t acquireCount() const;
    /// Acquires that found an empty pool (the decoder drops the frame).
    std::uint64_t droppedAcquires() const;
    std::uint64_t totalWaitUs() const;

private:
    AcquireFn m_acquireFn;
    void** m_slots = nullptr;
    int m_capacity = 0;
    int m_free = 0;
    mutable std::mutex m_mutex;
    std::atomic<std::uint64_t> m_acquired{0};
    std::atomic<std::uint64_t> m_dropped{0};
    std::atomic<std::uint64_t> m_waitUs{0};
};

/**
 * @brief Pipeline observability for a VideoSource (monotonic counters).
 *
 * Cheap to read from any thread; the UI may poll it for a stats overlay.
 */
struct VideoMetrics {
    VideoPath path = VideoPath::Unknown; ///< active pipeline path
    std::uint64_t framesDecoded = 0;     ///< frames emitted by the decoder
    std::uint64_t cpuCopies = 0;         ///< GPU->CPU readbacks (fallback only)
    std::uint64_t droppedFrames = 0;     ///< pool misses + late frames
    std::uint64_t decodeTimeUs = 0;      ///< wall time inside the decoder
    std::uint64_t poolWaitUs = 0;        ///< time waiting for a pool surface
    int activeSurfaces = 0;              ///< pool surfaces currently in flight
};

/// Zero-copy master switch (checked once at source creation): set
/// FIRMIR_MM_ZERO_COPY=0 to force the CPU fallback path everywhere.
bool zeroCopyEnabled();

} // namespace Multimedia
} // namespace Firmir
