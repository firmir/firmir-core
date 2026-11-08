// FFmpeg VideoSource backend plugin (firmir-mm-ffmpeg.so).
//
// Software path: avformat -> avcodec -> swscale (RGBA8) with pts pacing.
//
// Zero-copy path (Linux/VAAPI, built when libva + libva-drm are found):
// avcodec decodes into VAAPI surfaces (frames never leave GPU memory), each
// frame is exported with av_hwframe_map(AV_PIX_FMT_DRM_PRIME) to a dma-buf
// descriptor and wrapped in a GpuFrameHandle from the VideoFramePool.
// vaSyncSurface() before export guarantees the decoder has finished writing
// (decoder -> renderer sync). Consumers without dma-buf import support pull
// pixels through the frame's readback functor (av_hwframe_transfer_data +
// swscale) — the CPU fallback, counted in metrics().cpuCopies.

#include <firmir/log.hpp>
#include <firmir/multimedia/mmplugin.hpp>
#include <firmir/multimedia/videosource.hpp>
#include <firmir/render/types.hpp> // Fourcc::*

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#ifdef FIRMIR_MM_HAVE_VAAPI
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/hwcontext_vaapi.h>
#include <va/va.h>
#endif
}

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <utility>

namespace Firmir {
namespace Multimedia {
namespace {

const LoggingCategory& lcMmFfmpeg() {
    static const LoggingCategory category("multimedia.ffmpeg");
    return category;
}

#ifdef FIRMIR_MM_HAVE_VAAPI
VideoColorSpace toVideoColorSpace(enum AVColorSpace cs) {
    switch (cs) {
    case AVCOL_SPC_BT470BG:   // BT.601 625-line
    case AVCOL_SPC_SMPTE170M: // BT.601 525-line
        return VideoColorSpace::Bt601;
    case AVCOL_SPC_BT709:
        return VideoColorSpace::Bt709;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return VideoColorSpace::Bt2020;
    default:
        return VideoColorSpace::Unknown;
    }
}

/// GPU-resident VAAPI frame exported as dma-buf (DRM PRIME descriptor).
/// Owns the decoded AVFrame (VASurface reference) and the mapped descriptor;
/// destruction returns the surface to the ffmpeg hw frames pool.
class VaapiFrame : public GpuFrameHandle {
public:
    ~VaapiFrame() override {
        if (m_drm)
            av_freep(&m_drm);
        if (m_frame)
            av_frame_free(&m_frame);
        if (m_inFlight)
            --(*m_inFlight);
    }

    void reset(AVFrame* frame, AVDRMFrameDescriptor* drm,
               std::atomic<int>* inFlight) {
        m_frame = frame;   // owned
        m_drm = drm;       // owned
        m_inFlight = inFlight;
        if (m_inFlight)
            ++(*m_inFlight);
    }

    Backend backend() const override { return Backend::DmaBuf; }

    int planeCount() const override {
        if (!m_drm)
            return 0;
        int planes = 0;
        for (int l = 0; l < m_drm->nb_layers; ++l)
            planes += m_drm->layers[l].nb_planes;
        return planes;
    }
    // Flattens layers[i].planes[j] into a single plane list (NV12/P010 are
    // single-layer; multi-object single-layer layouts are handled).
    bool planeRef(int plane, int* object, int* offset,
                  int* pitch) const {
        int index = 0;
        for (int l = 0; l < m_drm->nb_layers; ++l) {
            const AVDRMLayerDescriptor* layer = &m_drm->layers[l];
            if (plane < index + layer->nb_planes) {
                const AVDRMPlaneDescriptor* p = &layer->planes[plane - index];
                *object = p->object_index;
                *offset = p->offset;
                *pitch = p->pitch;
                return true;
            }
            index += layer->nb_planes;
        }
        return false;
    }
    int planeFd(int plane) const override {
        int object = 0, offset = 0, pitch = 0;
        return planeRef(plane, &object, &offset, &pitch)
                   ? m_drm->objects[object].fd
                   : -1;
    }
    std::uint32_t planeOffset(int plane) const override {
        int object = 0, offset = 0, pitch = 0;
        planeRef(plane, &object, &offset, &pitch);
        return static_cast<std::uint32_t>(offset);
    }
    std::uint32_t planeStride(int plane) const override {
        int object = 0, offset = 0, pitch = 0;
        planeRef(plane, &object, &offset, &pitch);
        return static_cast<std::uint32_t>(pitch);
    }
    std::uint32_t drmFormat() const override {
        return m_drm && m_drm->nb_layers > 0 ? m_drm->layers[0].format : 0;
    }
    std::uint64_t drmModifier() const override {
        if (!m_drm || m_drm->nb_objects == 0)
            return Render::Fourcc::ModifierInvalid;
        return m_drm->objects[0].format_modifier;
    }
    /// VASurfaceID: stable per pool surface, used as the texture cache key.
    std::uintptr_t poolKey() const override {
        return m_frame
                   ? reinterpret_cast<std::uintptr_t>(m_frame->data[3])
                   : 0;
    }

    AVFrame* avFrame() const { return m_frame; }

private:
    AVFrame* m_frame = nullptr;
    AVDRMFrameDescriptor* m_drm = nullptr;
    std::atomic<int>* m_inFlight = nullptr;
};
#endif // FIRMIR_MM_HAVE_VAAPI

} // namespace
class FfmpegVideoSource final : public VideoSourceBackend {
    using State = VideoSource::State;

public:
    explicit FfmpegVideoSource(VideoSource* host) : VideoSourceBackend(host) {}
    ~FfmpegVideoSource() override { close(); }

    const char* backendName() const override { return "ffmpeg"; }
    VideoMetrics metrics() const override {
        VideoMetrics m;
        m.path = m_hwDecode ? VideoPath::ZeroCopy : VideoPath::CpuFallback;
        m.framesDecoded = m_frames.load();
        m.cpuCopies = m_cpuCopies.load();
        m.droppedFrames = m_dropped.load();
        m.decodeTimeUs = m_decodeUs.load();
        m.activeSurfaces = m_inFlight.load();
        return m;
    }

    bool open(const String& uri) override {
        close();
        const std::string path = uri.toStdString();
        AVDictionary* options = nullptr;
        if (avformat_open_input(&m_format, path.c_str(), nullptr, &options) <
            0) {
            emitError(String("cannot open: ") + uri);
            return false;
        }
        if (avformat_find_stream_info(m_format, nullptr) < 0) {
            emitError(String("no stream info: ") + uri);
            return false;
        }
        m_videoStream =
            av_find_best_stream(m_format, AVMEDIA_TYPE_VIDEO, -1, -1,
                                &m_decoder, 0);
        const int audioStream =
            av_find_best_stream(m_format, AVMEDIA_TYPE_AUDIO, -1, -1,
                                nullptr, 0);
        m_hasAudio = audioStream >= 0;
        if (m_videoStream < 0) {
            if (!m_hasAudio) {
                emitError(String("no audio/video streams"));
                return false;
            }
            // Audio-only: duration/position work, no video frames.
        }
        if (m_decoder) {
            m_codec = avcodec_alloc_context3(m_decoder);
            avcodec_parameters_to_context(
                m_codec, m_format->streams[m_videoStream]->codecpar);
            m_timeBase = m_format->streams[m_videoStream]->time_base;
#ifdef FIRMIR_MM_HAVE_VAAPI
            if (zeroCopyEnabled())
                tryEnableVaapi();
#endif
            if (avcodec_open2(m_codec, m_decoder, nullptr) < 0) {
                emitError(String("cannot open codec"));
                return false;
            }
        }
        m_duration = m_format->duration > 0
                         ? static_cast<double>(m_format->duration) /
                               AV_TIME_BASE
                         : -1.0;
        m_path = m_hwDecode ? VideoPath::ZeroCopy : VideoPath::CpuFallback;
        cLogInfo(lcMmFfmpeg())
            << "open:" << (m_hwDecode ? "zero-copy vaapi"
                                      : (zeroCopyEnabled()
                                             ? "cpu (no vaapi device)"
                                             : "cpu (FIRMIR_MM_ZERO_COPY=0)"));
        m_state = State::Stopped;
        return true;
    }

    void close() override {
        stopDecoding();
        if (m_codec) {
            avcodec_free_context(&m_codec);
            m_codec = nullptr;
        }
        if (m_format) {
            avformat_close_input(&m_format);
            m_format = nullptr;
        }
#ifdef FIRMIR_MM_HAVE_VAAPI
        if (m_hwDevice) {
            av_buffer_unref(&m_hwDevice);
            m_hwDevice = nullptr;
        }
#endif
        m_decoder = nullptr;
        m_videoStream = -1;
        m_hwDecode = false;
        m_position = 0;
        m_state = State::Stopped;
        m_hasAudio = false;
    }

    void play() override {
        if (!m_format || m_state == State::Playing)
            return;
        const bool resume = m_state == State::Paused;
        m_state = State::Playing;
        m_paused = false;
        emitStateChanged();
        if (!resume)
            startDecoding(); // the paused decode thread is still alive
    }
    void pause() override {
        if (m_state != State::Playing)
            return;
        m_state = State::Paused;
        m_paused = true;
        emitStateChanged();
    }
    void stop() override {
        if (!m_format)
            return;
        stopDecoding();
        if (m_videoStream >= 0)
            av_seek_frame(m_format, m_videoStream, 0, AVSEEK_FLAG_BACKWARD);
        m_position = 0;
        m_state = State::Stopped;
        emitStateChanged();
    }
    State state() const override { return m_state; }

    double duration() const override { return m_duration; }
    double position() const override { return m_position.load(); }

    void seek(double seconds) override {
        requestSeek(seconds);
    }

    void videoSize(int& width, int& height) const override {
        width = m_codec ? m_codec->width : 0;
        height = m_codec ? m_codec->height : 0;
    }
    bool hasVideo() const override { return m_videoStream >= 0; }
    bool hasAudio() const override { return m_hasAudio; }

private:
    void emitStateChanged() {
        emitState(m_state.load());
    }

    void requestSeek(double seconds) {
        m_seekTarget = seconds;
        m_seekRequested = true;
        m_position = seconds;
    }

    void stopDecoding() {
        m_decoding = false;
        if (m_decoder_thread.joinable())
            m_decoder_thread.join();
    }

    void startDecoding() {
        if (!m_codec)
            return;
        if (m_decoder_thread.joinable()) {
            // The previous decode thread finished (EOF): reclaim it so the
            // file can be replayed after seek.
            m_decoding = false;
            m_decoder_thread.join();
        }
        m_decoding = true;
        m_paused = false;
        m_decoder_thread = std::thread([this] { decodeLoop(); });
    }

#ifdef FIRMIR_MM_HAVE_VAAPI
    /// Attempts VAAPI hardware decoding: creates the device, wires the
    /// get_format callback. On any failure the source silently stays on the
    /// software path.
    void tryEnableVaapi() {
        if (av_hwdevice_ctx_create(&m_hwDevice, AV_HWDEVICE_TYPE_VAAPI,
                                   nullptr, nullptr, 0) < 0) {
            cLogInfo(lcMmFfmpeg())
                << "vaapi device unavailable; staying on the cpu path";
            return;
        }
        m_codec->opaque = this;
        m_codec->get_format = &FfmpegVideoSource::getFormat;
        m_codec->hw_device_ctx = av_buffer_ref(m_hwDevice);
        m_hwDecode = true;
    }

    /// get_format callback: prefer the VAAPI hardware pixel format.
    static enum AVPixelFormat getFormat(AVCodecContext* ctx,
                                        const enum AVPixelFormat* fmts) {
        for (const enum AVPixelFormat* f = fmts; *f != AV_PIX_FMT_NONE; ++f) {
            if (*f == AV_PIX_FMT_VAAPI)
                return AV_PIX_FMT_VAAPI;
        }
        auto* self = static_cast<FfmpegVideoSource*>(ctx->opaque);
        if (self)
            self->m_hwDecode = false; // decoder cannot do vaapi: fallback
        return avcodec_default_get_format(ctx, fmts);
    }

    /// Exports a decoded VAAPI frame as a dma-buf GpuFrameHandle from the
    /// pool. vaSyncSurface first: the decoder must have finished writing
    /// before the renderer imports the buffer (decoder -> renderer sync).
    void emitGpuFrame(AVFrame* decoded, double pts) {
        if (!m_pool) {
            // Bounded by the ffmpeg hw frames pool (initial_pool_size is
            // decoder-chosen); 8 covers the usual reorder depth + display
            // hold. The pool never blocks: a miss drops the frame.
            m_pool = std::make_unique<VideoFramePool>(
                8, [](void*) { return std::make_shared<VaapiFrame>(); });
        }
        auto handle = m_pool->acquire();
        if (!handle) {
            ++m_dropped;
            return;
        }
        AVHWDeviceContext* hwctx =
            reinterpret_cast<AVHWDeviceContext*>(m_hwDevice->data);
        AVVAAPIDeviceContext* vactx =
            static_cast<AVVAAPIDeviceContext*>(hwctx->hwctx);
        const VASurfaceID surface =
            static_cast<VASurfaceID>(
                reinterpret_cast<std::uintptr_t>(decoded->data[3]));
        vaSyncSurface(vactx->display, surface);

        AVFrame* mapped = av_frame_alloc();
        mapped->format = AV_PIX_FMT_DRM_PRIME;
        if (av_hwframe_map(mapped, decoded, AV_HWFRAME_MAP_READ) < 0) {
            av_frame_free(&mapped);
            ++m_dropped;
            return; // export failed: drop rather than fall back mid-stream
        }
        auto* vaapiFrame = static_cast<VaapiFrame*>(handle.get());
        vaapiFrame->reset(decoded,
                          static_cast<AVDRMFrameDescriptor*>(mapped->data[0]),
                          &m_inFlight);
        mapped->data[0] = nullptr; // descriptor ownership moved
        av_frame_free(&mapped);

        vaapiFrame->width = decoded->width;
        vaapiFrame->height = decoded->height;
        vaapiFrame->format =
            decoded->sw_format == AV_PIX_FMT_P010
                ? VideoPixelFormat::P010
                : VideoPixelFormat::Nv12;
        vaapiFrame->colorSpace = toVideoColorSpace(decoded->colorspace);
        vaapiFrame->colorRange = decoded->color_range == AVCOL_RANGE_JPEG
                                     ? VideoColorRange::Full
                                     : VideoColorRange::Limited;
        vaapiFrame->pts = pts;

        VideoFrame frame;
        frame.width = decoded->width;
        frame.height = decoded->height;
        frame.pts = pts;
        frame.gpu = handle;
        frame.readback = [this, handle](VideoFrame& f) {
            return readbackFrame(*handle, f);
        };
        m_position = pts;
        ++m_frames;
        emitFrame(std::move(frame));
    }

    /// Fallback GPU->CPU readback for a VAAPI frame (NV12 download +
    /// swscale). Counted as a CPU copy; never runs on the zero-copy path.
    bool readbackFrame(VaapiFrame& handle, VideoFrame& out) {
        AVFrame* sw = av_frame_alloc();
        if (!sw)
            return false;
        sw->format = AV_PIX_FMT_NV12;
        bool ok = av_hwframe_transfer_data(sw, handle.avFrame(), 0) >= 0;
        if (ok) {
            SwsContext* sws = sws_getCachedContext(
                nullptr, sw->width, sw->height, AV_PIX_FMT_NV12, sw->width,
                sw->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr,
                nullptr);
            out.width = sw->width;
            out.height = sw->height;
            out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
            std::uint8_t* dst[4] = {out.rgba.data(), nullptr, nullptr,
                                    nullptr};
            const int stride[4] = {out.width * 4, 0, 0, 0};
            sws_scale(sws, sw->data, sw->linesize, 0, sw->height, dst,
                      stride);
            sws_freeContext(sws);
            ++m_cpuCopies;
        }
        av_frame_free(&sw);
        return ok;
    }
#endif // FIRMIR_MM_HAVE_VAAPI

    static std::uint64_t
    elapsedUs(std::chrono::steady_clock::time_point start) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start)
                .count());
    }

    void decodeLoop() {
        AVPacket* packet = av_packet_alloc();
        AVFrame* frame = av_frame_alloc();
        SwsContext* sws = nullptr;
        m_clockStart = av_gettime_relative() / 1.0e6;
        bool clockAnchored = false;
        while (m_decoding) {
            if (m_paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (m_seekRequested.exchange(false)) {
                const int64_t ts =
                    static_cast<int64_t>(m_seekTarget / av_q2d(m_timeBase));
                av_seek_frame(m_format, m_videoStream, ts,
                              AVSEEK_FLAG_BACKWARD);
                avcodec_flush_buffers(m_codec);
                clockAnchored = false;
            }
            const int rc = av_read_frame(m_format, packet);
            if (rc < 0) { // EOF or an unrecoverable read error
                m_state = State::Stopped;
                emitStateChanged();
                break;
            }
            if (packet->stream_index != m_videoStream) {
                av_packet_unref(packet); // audio etc.: demuxed, not played
                continue;
            }
            // decodeTimeUs covers the codec calls only — not the pacing sleep.
            auto decodeStart = std::chrono::steady_clock::now();
            if (avcodec_send_packet(m_codec, packet) >= 0) {
                m_decodeUs += elapsedUs(decodeStart);
                for (;;) {
                    decodeStart = std::chrono::steady_clock::now();
                    const int got = avcodec_receive_frame(m_codec, frame);
                    m_decodeUs += elapsedUs(decodeStart);
                    if (got < 0)
                        break;
                    const double pts =
                        frame->best_effort_timestamp == AV_NOPTS_VALUE
                            ? m_position.load()
                            : frame->best_effort_timestamp *
                                  av_q2d(m_timeBase);
                    if (!clockAnchored) {
                        m_clockStart = av_gettime_relative() / 1.0e6 - pts;
                        clockAnchored = true;
                    }
                    paceFrame(pts);
                    if (frame->format == AV_PIX_FMT_VAAPI) {
#ifdef FIRMIR_MM_HAVE_VAAPI
                        // Hand the decoded surface to the GPU frame path;
                        // ownership of `frame` moves into the handle.
                        AVFrame* owned = av_frame_alloc();
                        av_frame_move_ref(owned, frame);
                        emitGpuFrame(owned, pts);
#else
                        ++m_dropped; // vaapi frame without vaapi build
#endif
                    } else {
                        deliverCpuFrame(frame, sws, pts);
                    }
                    if (m_paused || m_seekRequested)
                        break;
                }
            }
            av_packet_unref(packet);
        }
        sws_freeContext(sws);
        av_frame_free(&frame);
        av_packet_free(&packet);
    }

    /// Paces frame delivery against the wall clock (at most 5 s of drift is
    /// compensated); runs on the decode thread, never on the render path.
    void paceFrame(double pts) {
        const double due = m_clockStart + pts;
        double delay = due - av_gettime_relative() / 1.0e6;
        if (delay > 5.0)
            delay = 0.0;
        if (delay > 0)
            std::this_thread::sleep_for(std::chrono::microseconds(
                static_cast<int64_t>(delay * 1e6)));
    }

    /// CPU path: swscale to RGBA8 and emit (existing behavior).
    void deliverCpuFrame(AVFrame* frame, SwsContext*& sws, double pts) {
        sws = sws_getCachedContext(sws, frame->width, frame->height,
                                   static_cast<AVPixelFormat>(frame->format),
                                   frame->width, frame->height,
                                   AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                                   nullptr, nullptr);
        if (!sws)
            return;
        VideoFrame out;
        out.width = frame->width;
        out.height = frame->height;
        out.pts = pts;
        out.rgba.resize(static_cast<size_t>(out.width) * out.height * 4);
        std::uint8_t* dst[4] = {out.rgba.data(), nullptr, nullptr, nullptr};
        const int stride[4] = {out.width * 4, 0, 0, 0};
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height, dst,
                  stride);
        m_position = pts;
        ++m_frames;
        emitFrame(std::move(out));
    }

    AVFormatContext* m_format = nullptr;
    const AVCodec* m_decoder = nullptr;
    AVCodecContext* m_codec = nullptr;
    int m_videoStream = -1;
    AVRational m_timeBase = {1, 1};
    double m_duration = -1.0;
    bool m_hasAudio = false;

    std::thread m_decoder_thread;
    std::atomic<bool> m_decoding{false};
    std::atomic<bool> m_paused{false};
    std::atomic<State> m_state{State::Stopped};
    std::atomic<double> m_position{0.0};
    double m_clockStart = 0.0;
    std::atomic<bool> m_seekRequested{false};
    std::atomic<double> m_seekTarget{0.0};

    // Zero-copy state (VAAPI build only) + metrics.
    bool m_hwDecode = false;
    VideoPath m_path = VideoPath::Unknown;
    std::atomic<std::uint64_t> m_frames{0};
    std::atomic<std::uint64_t> m_cpuCopies{0};
    std::atomic<std::uint64_t> m_dropped{0};
    std::atomic<std::uint64_t> m_decodeUs{0};
    std::atomic<int> m_inFlight{0};
#ifdef FIRMIR_MM_HAVE_VAAPI
    AVBufferRef* m_hwDevice = nullptr;
    std::unique_ptr<VideoFramePool> m_pool;
#endif
};

/// Plugin entry (firmir-mm-ffmpeg.so).
const MmSourcePlugin* ffmpegPlugin() {
    static const MmSourcePlugin plugin = {
        FIRMIR_MM_PLUGIN_ABI, "ffmpeg",
        []() -> bool { return true; }, // libav* are linked in
        [](VideoSource* host) -> VideoSourceBackend* {
            return new FfmpegVideoSource(host);
        }};
    return &plugin;
}

} // namespace Multimedia
} // namespace Firmir

extern "C" const Firmir::Multimedia::MmSourcePlugin* firmir_mm_plugin() {
    return Firmir::Multimedia::ffmpegPlugin();
}

