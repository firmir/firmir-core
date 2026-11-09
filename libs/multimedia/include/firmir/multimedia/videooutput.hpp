#pragma once

// Firmir::Multimedia::VideoOutput — an Item that shows the frames of a
// VideoSource. The source emits frameReady from its decoder thread; the
// frames are coalesced (only the newest one is handed to the GUI thread) so
// a slow renderer drops frames instead of piling them up in the event
// queue — an unprocessed 4K RGBA frame is ~33 MB, and a growing queue shows
// up as video progressively lagging behind realtime ("slow motion").

#include <firmir/gui/item.hpp>
#include <firmir/multimedia/videosource.hpp>
#include <firmir/multimedia/videotexture.hpp>

#include <atomic>
#include <mutex>

namespace Firmir {
namespace Multimedia {

class VideoOutput : public Gui::Item {
public:
    /// How the frame is fitted into the item's rectangle (mirrors
    /// Gui::Image::FillMode):
    /// - Stretch: fill the item, ignoring the frame's aspect ratio;
    /// - PreserveAspectFit: scale uniformly to fit inside, letterboxing the
    ///   remainder (default);
    /// - PreserveAspectCrop: scale uniformly to cover the whole item,
    ///   cropping the overflow (the item clips);
    /// - Pad: draw the frame unscaled (1:1 pixels), centered; overflow is
    ///   clipped, the remainder stays black.
    enum class FillMode { Stretch, PreserveAspectFit, PreserveAspectCrop, Pad };

    explicit VideoOutput(Object* parent = nullptr);
    ~VideoOutput() override;

    /// The source is not owned; it must outlive the VideoOutput.
    void setSource(VideoSource* source);
    VideoSource* source() const { return m_source; }

    FillMode fillMode() const { return m_fillMode; }
    void setFillMode(FillMode mode);
    /// String form for the dynamic property system / Firml
    /// ("Stretch", "PreserveAspectFit", "PreserveAspectCrop", "Pad").
    /// Unknown values are ignored.
    void setFillModeName(const String& name);
    String fillModeName() const;

    /// Consumer-side counters: frames drawn straight from GPU memory versus
    /// frames that needed the CPU readback fallback (renderer without dma-buf
    /// import, or FIRMIR_MM_ZERO_COPY=0).
    std::uint64_t zeroCopyFrames() const { return m_zeroCopyFrames; }
    std::uint64_t cpuFallbackFrames() const { return m_cpuFallbackFrames; }

protected:
    void paint(Gui::Painter& painter) override;
    bool event(Event& e) override;

private:
    void onFrame(const VideoFrame& frame);        // GUI thread: show the frame
    void onFrameDecoded(const VideoFrame& frame); // decoder thread: coalesce

    VideoSource* m_source = nullptr;
    Connection m_frameConnection;
    // Latest decoded frame waiting for the GUI thread; guarded by the mutex.
    // m_framePosted is set while a "frame pending" event sits in the GUI
    // queue, so the decoder posts at most one event per consumed frame.
    std::mutex m_pendingMutex;
    VideoFrame m_pendingFrame;
    bool m_pendingValid = false;
    std::atomic<bool> m_framePosted{false};
    VideoFrame m_frame;
    /// Previously shown frame: its GPU surface is released one frame late so
    /// the renderer never races a surface the decoder has reclaimed
    /// (renderer -> decoder sync without fences; see doc/multimedia/zero-copy.md).
    VideoFrame m_previous;
    /// Per-frame generation for the CPU pixel upload cache.
    std::uint64_t m_generation = 0;
    /// Per-source generation for the GPU import cache: a pool surface is
    /// imported once and reused until the source changes.
    std::uint64_t m_sourceGeneration = 0;
    VideoTextureCache m_gpuTextures;
    FillMode m_fillMode = FillMode::PreserveAspectFit;
    std::uint64_t m_zeroCopyFrames = 0;
    std::uint64_t m_cpuFallbackFrames = 0;
};

} // namespace Multimedia
} // namespace Firmir
