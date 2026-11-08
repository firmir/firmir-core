#pragma once

// Firmir::Multimedia::VideoTextureCache — turns GpuFrameHandle video frames
// into render-device textures (dma-buf import) and draws them through
// Firmir::Gui::Painter.
//
// This is the consumer half of the zero-copy pipeline: the Painter stays
// media-agnostic (it only knows how to draw an arbitrary RenderTexture,
// external or not); the dma-buf descriptor mapping and the per-surface
// import cache live here, next to the decoder backends that produce the
// frames.

#include <firmir/gui/painter.hpp>
#include <firmir/multimedia/videosource.hpp>

#include <cstdint>
#include <map>
#include <memory>

namespace Firmir {
namespace Multimedia {

/**
 * @brief Imports GPU frames as textures (cached per decoder pool surface)
 * and draws them with a Painter.
 *
 * One cache per video consumer (e.g. VideoOutput): entries are keyed by
 * GpuFrameHandle::poolKey(), so a surface is imported once and reused while
 * the decoder pool reuses it. @c generation invalidates stale imports when
 * the source changes (a new decoder pool may recycle pool keys).
 *
 * All methods must be called from the thread that paints (the GUI thread).
 */
class VideoTextureCache {
public:
    VideoTextureCache() = default;
    ~VideoTextureCache() = default;

    VideoTextureCache(const VideoTextureCache&) = delete;
    VideoTextureCache& operator=(const VideoTextureCache&) = delete;

    /**
     * @brief Draws @p frame's GPU handle into @p target, zero-copy.
     *
     * Imports the dma-buf through the painter's render device on first use
     * of the pool surface; YUV -> RGB conversion happens in the GPU sampler.
     * Returns false when the frame has no GPU handle or the device cannot
     * import it — the caller then falls back to VideoFrame::ensureCpuPixels()
     * + Painter::drawRgba(). Once a device reported "unsupported" the cache
     * stops retrying and always returns false.
     */
    bool draw(Gui::Painter& painter, const Gui::RectF& target,
              const VideoFrame& frame, std::uint64_t generation);

    /// Drops all imported textures (e.g. on source change).
    void clear();

private:
    struct Entry {
        std::shared_ptr<Render::RenderTexture> texture;
        std::uint64_t generation = 0;
    };
    std::map<const void*, Entry> m_entries; ///< by GpuFrameHandle::poolKey()
    bool m_unsupported = false;
};

} // namespace Multimedia
} // namespace Firmir
