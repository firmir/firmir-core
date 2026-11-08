#pragma once

// Firmir::Multimedia::VideoSource — backend-neutral media source (the
// IVideoDecoder role of the zero-copy pipeline).
//
// A source decodes a URI (file path or URL) into video frames emitted
// through the "frameReady" signal; the audio track is played by the backend
// itself when it supports audio output. A frame either carries CPU pixels
// (VideoFrame::rgba, the fallback path) or a GpuFrameHandle (VideoFrame::gpu,
// the zero-copy path); backends also attach a readback functor so a consumer
// without dma-buf import support can pull CPU pixels on demand.
//
// Decode backends are loadable plugins (plugins/multimedia/, firmir-mm-*.so);
// availableVideoSourceBackends() lists what is present at runtime.

#include <firmir/multimedia/gpuframe.hpp>
#include <firmir/object.hpp>
#include <firmir/string.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Firmir {
namespace Multimedia {

/**
 * @brief One decoded video frame.
 *
 * CPU path: tightly packed RGBA8 in rgba, straight alpha.
 * Zero-copy path: gpu holds the opaque GPU frame; rgba stays empty until
 * someone calls ensureCpuPixels() (the consumer-side fallback).
 */
struct VideoFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; ///< width * height * 4 bytes (CPU path)
    double pts = 0.0;               ///< presentation time, seconds

    /// GPU-resident frame (zero-copy path); when set, rgba may be empty.
    std::shared_ptr<GpuFrameHandle> gpu;
    /// Backend-provided GPU->CPU readback used by ensureCpuPixels(); fills
    /// rgba/width/height. Only ever invoked on the fallback path.
    std::function<bool(VideoFrame&)> readback;

    bool isValid() const {
        return width > 0 && height > 0 && (!rgba.empty() || gpu != nullptr);
    }
    bool hasGpuPixels() const { return gpu != nullptr; }
    /// Ensures CPU pixels are available, reading the GPU frame back through
    /// the backend's functor when needed. Returns false when the frame has
    /// no pixels at all (dropped/undecodable).
    bool ensureCpuPixels();
};

class VideoSourceBackend;

/**
 * @brief Media source: decodes a URI into frames, delegating the actual
 * work to a VideoSourceBackend (a runtime plugin or a custom in-process
 * implementation).
 *
 * VideoSource itself is concrete and backend-neutral; the constructor picks
 * the backend by name ("gstreamer", "ffmpeg", "auto"), the public methods
 * forward to it, and the backend reports frames/state/errors back through
 * the emit*() functions, which raise the corresponding signals.
 */
class VideoSource : public Object {
public:
    enum class State { Stopped, Playing, Paused };

    /// Uses the "auto"-selected backend plugin (isValid() reports whether
    /// one was found).
    explicit VideoSource(Object* parent = nullptr);
    /// Uses the named backend plugin ("gstreamer", "ffmpeg", "auto").
    VideoSource(const String& backend, Object* parent = nullptr);
    /// Uses a custom in-process backend (tests, embedders); takes ownership.
    VideoSource(std::unique_ptr<VideoSourceBackend> backend,
                Object* parent = nullptr);
    ~VideoSource() override;

    /// False when no backend plugin was found; all operations then fail
    /// gracefully (open() returns false, state() stays Stopped).
    bool isValid() const;

    /// Opens @p uri (a local path or a URL). Returns false on failure; the
    /// "error" signal carries a human-readable message.
    bool open(const String& uri);
    void close();

    void play();
    void pause();
    void stop();
    State state() const;

    double duration() const; ///< seconds; < 0 when unknown
    double position() const; ///< seconds
    void seek(double seconds);

    void videoSize(int& width, int& height) const;
    bool hasVideo() const;
    bool hasAudio() const;

    /// Backend identifier ("gstreamer", "ffmpeg"); "none" when invalid.
    const char* backendName() const;

    /// Pipeline observability counters (path, decode time, pool stats).
    VideoMetrics metrics() const;

    /// Emitted on a backend worker thread for every decoded frame; connect
    /// with ConnectionMode::Queued. Argument: VideoFrame.
    static constexpr auto signalFrameReady = "frameReady";
    /// Emitted on state transitions. Argument: int (State).
    static constexpr auto signalStateChanged = "stateChanged";
    /// Emitted on errors. Argument: String message.
    static constexpr auto signalError = "error";

    // ---- backend-facing (called by VideoSourceBackend implementations) ----
    void emitFrameReady(VideoFrame frame);
    void emitStateChanged(State state);
    void emitErrorMessage(const String& message);

private:
    std::unique_ptr<VideoSourceBackend> m_backend;
};

/// Creates a source for @p backend ("gstreamer", "ffmpeg"; "auto" picks the
/// first available plugin). Returns nullptr when no plugin was found.
VideoSource* createVideoSource(const String& backend = String("auto"),
                               Object* parent = nullptr);

/// Names of the installed backend plugins, in "auto" preference order.
std::vector<std::string> availableVideoSourceBackends();

} // namespace Multimedia
} // namespace Firmir
