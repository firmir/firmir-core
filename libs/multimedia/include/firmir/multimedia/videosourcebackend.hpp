#pragma once

// Firmir::Multimedia::VideoSourceBackend — the decode backend interface
// implemented by the multimedia plugins (plugins/multimedia/, e.g. ffmpeg
// and gstreamer) and by custom in-process backends (tests, embedders).
//
// A backend is owned by its VideoSource host and never emits signals
// directly: it reports through the emit*() helpers, which raise the public
// VideoSource signals. The decode thread contract is unchanged: frames may
// be emitted from a backend worker thread; consumers connect with
// ConnectionMode::Queued.

#include <firmir/multimedia/videosource.hpp>

namespace Firmir {
namespace Multimedia {

class VideoSourceBackend {
public:
    /// @p host may be null when the backend is created before its
    /// VideoSource (the source then adopts it, see the unique_ptr
    /// constructor); plugins always receive a valid host.
    explicit VideoSourceBackend(VideoSource* host = nullptr) : m_host(host) {}
    virtual ~VideoSourceBackend() = default;

    virtual bool open(const String& uri) = 0;
    virtual void close() = 0;

    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual VideoSource::State state() const = 0;

    virtual double duration() const = 0; ///< seconds; < 0 when unknown
    virtual double position() const = 0; ///< seconds
    virtual void seek(double seconds) = 0;

    virtual void videoSize(int& width, int& height) const = 0;
    virtual bool hasVideo() const = 0;
    virtual bool hasAudio() const = 0;

    /// Backend identifier ("gstreamer", "ffmpeg").
    virtual const char* backendName() const = 0;

    /// Pipeline observability counters (path, decode time, pool stats).
    virtual VideoMetrics metrics() const { return {}; }

protected:
    VideoSource* host() const { return m_host; }

    /// Report a decoded frame (may be called from a worker thread).
    void emitFrame(VideoFrame frame) { m_host->emitFrameReady(std::move(frame)); }
    /// Report a state transition.
    void emitState(VideoSource::State state) { m_host->emitStateChanged(state); }
    /// Report a human-readable error.
    void emitError(const String& message) { m_host->emitErrorMessage(message); }

private:
    friend class VideoSource; // adopts a host-less backend (setHost)
    void setHost(VideoSource* host) { m_host = host; }

    VideoSource* m_host;
};

} // namespace Multimedia
} // namespace Firmir
