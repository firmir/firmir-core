#include <firmir/multimedia/videooutput.hpp>

#include <firmir/gui/painter.hpp>

#include <algorithm>

namespace Firmir {
namespace Multimedia {

VideoOutput::VideoOutput(Object* parent) : Gui::Item(parent) {
    setClip(true);
    registerPropertyImpl("fillMode", typeid(Firmir::String),
        [](const Object* o) -> std::any { return static_cast<const VideoOutput*>(o)->fillModeName(); },
        [](Object* o, const std::any& v) { static_cast<VideoOutput*>(o)->setFillModeName(std::any_cast<Firmir::String>(v)); });
}

VideoOutput::~VideoOutput() { setSource(nullptr); }

void VideoOutput::setSource(VideoSource* source) {
    if (m_source == source)
        return;
    m_frameConnection.disconnect();
    m_source = source;
    m_frame = VideoFrame();
    ++m_generation;
    ++m_sourceGeneration; // a new decoder pool may recycle pool keys
    update();
    if (m_source) {
        // The decoder emits from its worker thread; queue into the GUI thread.
        m_frameConnection = m_source->connect<VideoFrame>(
            this, VideoSource::signalFrameReady,
            [this](const VideoFrame& frame) { onFrame(frame); },
            ConnectionMode::Queued);
    }
}

void VideoOutput::setFillMode(FillMode mode) {
    if (m_fillMode == mode)
        return;
    m_fillMode = mode;
    update();
    notifyPropertyChanged("fillMode");
}

String VideoOutput::fillModeName() const {
    switch (m_fillMode) {
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
    m_previous = m_frame;
    m_frame = frame;
    ++m_generation;
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
    if (!m_frame.isValid())
        return;
    const Gui::RectF target = targetRect(m_fillMode, width(), height(),
                                         m_frame.width, m_frame.height);
    // Zero-copy: import the dma-buf straight from the decoder pool (cached
    // per surface) and sample it on the GPU; YUV -> RGB in the sampler.
    if (m_frame.gpu && zeroCopyEnabled() &&
        m_gpuTextures.draw(painter, target, m_frame, m_sourceGeneration)) {
        ++m_zeroCopyFrames;
        return;
    }
    // CPU fallback: pull pixels through the backend's readback (counted).
    if (!m_frame.ensureCpuPixels())
        return;
    if (m_frame.gpu)
        ++m_cpuFallbackFrames;
    painter.drawRgba(target, m_frame.rgba.data(), m_frame.width,
                     m_frame.height, this, m_generation);
}

} // namespace Multimedia
} // namespace Firmir

