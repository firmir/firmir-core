#include <firmir/multimedia/videooutput.hpp>

#include <firmir/gui/painter.hpp>

#include <algorithm>

namespace Firmir {
namespace Multimedia {

namespace {

/// Event type posted (at most one at a time) when a decoded frame waits in
/// the pending slot.
constexpr int kFramePendingEvent = Event::User + 400;

} // namespace


class VideoOutput::Impl {
public:
    VideoSource* source = nullptr;
    Connection frameConnection;
    // Latest decoded frame waiting for the GUI thread; guarded by the mutex.
    // framePosted is set while a "frame pending" event sits in the GUI
    // queue, so the decoder posts at most one event per consumed frame.
    std::mutex pendingMutex;
    VideoFrame pendingFrame;
    bool pendingValid = false;
    std::atomic<bool> framePosted{false};
    VideoFrame frame;
    /// Previously shown frame: its GPU surface is released one frame late so
    /// the renderer never races a surface the decoder has reclaimed
    /// (renderer -> decoder sync without fences; see doc/multimedia/zero-copy.md).
    VideoFrame previous;
    /// Per-frame generation for the CPU pixel upload cache.
    std::uint64_t generation = 0;
    /// Per-source generation for the GPU import cache: a pool surface is
    /// imported once and reused until the source changes.
    std::uint64_t sourceGeneration = 0;
    VideoTextureCache gpuTextures;
    FillMode fillMode = FillMode::PreserveAspectFit;
    std::uint64_t zeroCopyFrames = 0;
    std::uint64_t cpuFallbackFrames = 0;
};

VideoOutput::VideoOutput(Object* parent) : Gui::Item(parent), d(std::make_unique<Impl>()) {
    setClip(true);
    registerPropertyImpl("fillMode", typeid(Firmir::String),
        [](const Object* o) -> std::any { return static_cast<const VideoOutput*>(o)->fillModeName(); },
        [](Object* o, const std::any& v) { static_cast<VideoOutput*>(o)->setFillModeName(std::any_cast<Firmir::String>(v)); });
}

VideoOutput::~VideoOutput() { setSource(nullptr); }

VideoSource* VideoOutput::source() const { return d->source; }
VideoOutput::FillMode VideoOutput::fillMode() const { return d->fillMode; }
std::uint64_t VideoOutput::zeroCopyFrames() const { return d->zeroCopyFrames; }
std::uint64_t VideoOutput::cpuFallbackFrames() const { return d->cpuFallbackFrames; }

void VideoOutput::setSource(VideoSource* source) {
    if (d->source == source)
        return;
    d->frameConnection.disconnect();
    d->source = source;
    d->frame = VideoFrame();
    {
        std::lock_guard<std::mutex> lock(d->pendingMutex);
        d->pendingFrame = VideoFrame();
        d->pendingValid = false;
    }
    ++d->generation;
    ++d->sourceGeneration; // a new decoder pool may recycle pool keys
    update();
    if (d->source) {
        // The decoder emits from its worker thread; a direct connection feeds
        // the coalescing slot, which posts a single event per shown frame.
        d->frameConnection = d->source->connect<VideoFrame>(
            this, VideoSource::signalFrameReady,
            [this](const VideoFrame& frame) { onFrameDecoded(frame); },
            ConnectionMode::Direct);
    }
}

void VideoOutput::onFrameDecoded(const VideoFrame& frame) {
    {
        std::lock_guard<std::mutex> lock(d->pendingMutex);
        d->pendingFrame = frame;
        d->pendingValid = true;
    }
    if (!d->framePosted.exchange(true))
        Object::postEvent(this, std::make_unique<Event>(kFramePendingEvent));
}

bool VideoOutput::event(Event& e) {
    if (e.type() == kFramePendingEvent) {
        d->framePosted.store(false);
        VideoFrame frame;
        {
            std::lock_guard<std::mutex> lock(d->pendingMutex);
            if (d->pendingValid) {
                frame = std::move(d->pendingFrame);
                d->pendingFrame = VideoFrame();
                d->pendingValid = false;
            }
        }
        if (frame.isValid())
            onFrame(frame);
        return true;
    }
    return Gui::Item::event(e);
}

void VideoOutput::setFillMode(FillMode mode) {
    if (d->fillMode == mode)
        return;
    d->fillMode = mode;
    update();
    notifyPropertyChanged("fillMode");
}

String VideoOutput::fillModeName() const {
    switch (d->fillMode) {
    case FillMode::Stretch: return String("Stretch");
    case FillMode::PreserveAspectCrop: return String("PreserveAspectCrop");
    case FillMode::Pad: return String("Pad");
    default: return String("PreserveAspectFit");
    }
}
void VideoOutput::setFillModeName(const String& name) {
    const std::string value = name.toStdString();
    if (value == "Stretch")
        setFillMode(FillMode::Stretch);
    else if (value == "PreserveAspectFit")
        setFillMode(FillMode::PreserveAspectFit);
    else if (value == "PreserveAspectCrop")
        setFillMode(FillMode::PreserveAspectCrop);
    else if (value == "Pad")
        setFillMode(FillMode::Pad);
}

void VideoOutput::onFrame(const VideoFrame& frame) {
    // Deferred release: the outgoing frame's GPU surface is returned to the
    // decoder pool only after the next frame has been shown, so the decoder
    // never overwrites a surface the renderer may still sample.
    d->previous = d->frame;
    d->frame = frame;
    ++d->generation;
    update();
}

namespace {

/// Maps the frame into the item rectangle for the active fill mode; the
/// item clips, so PreserveAspectCrop simply overflows.
Gui::RectF targetRect(VideoOutput::FillMode mode, float itemW, float itemH,
                      int frameW, int frameH) {
    switch (mode) {
    case VideoOutput::FillMode::PreserveAspectFit: {
        const float s = std::min(itemW / static_cast<float>(frameW),
                                 itemH / static_cast<float>(frameH));
        const float w = frameW * s, h = frameH * s;
        return {(itemW - w) / 2.0f, (itemH - h) / 2.0f, w, h};
    }
    case VideoOutput::FillMode::PreserveAspectCrop: {
        const float s = std::max(itemW / static_cast<float>(frameW),
                                 itemH / static_cast<float>(frameH));
        const float w = frameW * s, h = frameH * s;
        return {(itemW - w) / 2.0f, (itemH - h) / 2.0f, w, h};
    }
    case VideoOutput::FillMode::Pad: {
        const float w = static_cast<float>(frameW),
                    h = static_cast<float>(frameH);
        return {(itemW - w) / 2.0f, (itemH - h) / 2.0f, w, h};
    }
    case VideoOutput::FillMode::Stretch:
        break;
    }
    return {0, 0, itemW, itemH};
}

} // namespace

void VideoOutput::paint(Gui::Painter& painter) {
    painter.fillRect(Gui::RectF{0, 0, width(), height()},
                     Gui::Color::rgb(0, 0, 0));
    if (!d->frame.isValid())
        return;
    const Gui::RectF target = targetRect(d->fillMode, width(), height(),
                                         d->frame.width, d->frame.height);
    // Zero-copy: import the dma-buf straight from the decoder pool (cached
    // per surface) and sample it on the GPU; YUV -> RGB in the sampler.
    if (d->frame.gpu && zeroCopyEnabled() &&
        d->gpuTextures.draw(painter, target, d->frame, d->sourceGeneration)) {
        ++d->zeroCopyFrames;
        return;
    }
    // CPU fallback: pull pixels through the backend's readback (counted).
    if (!d->frame.ensureCpuPixels())
        return;
    if (d->frame.gpu)
        ++d->cpuFallbackFrames;
    painter.drawRgba(target, d->frame.rgba.data(), d->frame.width,
                     d->frame.height, this, d->generation);
}

} // namespace Multimedia
} // namespace Firmir

