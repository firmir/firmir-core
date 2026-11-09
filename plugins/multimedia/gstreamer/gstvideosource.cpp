// GStreamer VideoSource backend plugin (firmir-mm-gstreamer.so).
//
// A playbin whose video is routed into an appsink and whose audio goes to
// the default audio sink. Frames are pulled on a worker thread; the
// appsink's sync=true paces them against the pipeline clock.
//
// Zero-copy: when FIRMIR_MM_ZERO_COPY != 0 the appsink first offers
// video/x-raw(memory:DMABuf) caps (NV12); hardware decoders (vaapi/v4l2)
// negotiate them and frames are exported as dma-buf GpuFrameHandles without
// any CPU copy. Pipelines without a dma-buf-capable decoder negotiate plain
// RGBA instead (CPU fallback path) — no caller changes needed.

#include <firmir/log.hpp>
#include <firmir/multimedia/mmplugin.hpp>
#include <firmir/multimedia/videosource.hpp>
#include <firmir/render/types.hpp> // Fourcc::*

#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/allocators/gstdmabuf.h>
#include <gst/video/video.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

namespace Firmir {
namespace Multimedia {
namespace {

const LoggingCategory& lcMmGst() {
    static const LoggingCategory category("multimedia.gstreamer");
    return category;
}

/// GPU-resident frame: wraps a GstSample whose buffer carries dma-buf
/// memory. The sample (and thus the pool surface) stays alive until the
/// handle dies; poolKey is the GstBuffer pointer — stable across
/// buffer-pool reuse, so the renderer caches one imported texture per
/// surface instead of importing per frame.
class GstDmaBufFrame : public GpuFrameHandle {
public:
    GstDmaBufFrame(GstSample* sample, GstBuffer* buffer, GstVideoMeta* meta,
                   int planes, std::atomic<int>& inFlight)
        : m_sample(sample), m_inFlight(inFlight) {
        ++m_inFlight;
        for (int i = 0; i < planes && i < 4; ++i) {
            GstMemory* mem = gst_buffer_peek_memory(buffer, i);
            m_fds[i] = mem ? gst_dmabuf_memory_get_fd(mem) : -1;
            m_offsets[i] =
                static_cast<std::uint32_t>(meta ? meta->offset[i] : 0);
            m_strides[i] =
                static_cast<std::uint32_t>(meta ? meta->stride[i] : 0);
        }
        m_planeCount = planes;
    }
    ~GstDmaBufFrame() override {
        --m_inFlight;
        if (m_sample)
            gst_sample_unref(m_sample);
    }

    Backend backend() const override { return Backend::DmaBuf; }
    int planeCount() const override { return m_planeCount; }
    int planeFd(int plane) const override {
        return plane >= 0 && plane < m_planeCount ? m_fds[plane] : -1;
    }
    std::uint32_t planeOffset(int plane) const override {
        return m_offsets[plane];
    }
    std::uint32_t planeStride(int plane) const override {
        return m_strides[plane];
    }
    std::uint32_t drmFormat() const override { return Render::Fourcc::Nv12; }
    std::uint64_t drmModifier() const override {
        return Render::Fourcc::ModifierInvalid;
    }
    std::uintptr_t poolKey() const override {
        return reinterpret_cast<std::uintptr_t>(
            m_sample ? gst_sample_get_buffer(m_sample) : nullptr);
    }

private:
    GstSample* m_sample; // owned
    std::atomic<int>& m_inFlight;
    int m_planeCount = 0;
    int m_fds[4] = {-1, -1, -1, -1};
    std::uint32_t m_offsets[4] = {0, 0, 0, 0};
    std::uint32_t m_strides[4] = {0, 0, 0, 0};
};

/// NV12 -> RGBA8 conversion, used ONLY on the CPU fallback path (renderer
/// cannot import dma-buf). Straight BT.601 limited-range matrix.
bool nv12ToRgba(const std::uint8_t* y, const std::uint8_t* uv, int width,
                int height, int yStride, int uvStride,
                std::vector<std::uint8_t>& out) {
    if (!y || !uv || width <= 0 || height <= 0)
        return false;
    out.resize(static_cast<size_t>(width) * height * 4);
    for (int row = 0; row < height; ++row) {
        const std::uint8_t* yRow = y + static_cast<size_t>(row) * yStride;
        const std::uint8_t* uvRow =
            uv + static_cast<size_t>(row / 2) * uvStride;
        std::uint8_t* dst = out.data() + static_cast<size_t>(row) * width * 4;
        for (int col = 0; col < width; ++col) {
            const int yy = yRow[col];
            const int u = uvRow[(col & ~1)] - 128;
            const int v = uvRow[(col & ~1) + 1] - 128;
            const int c = yy - 16;
            const int r = (298 * c + 409 * v + 128) >> 8;
            const int g = (298 * c - 100 * u - 208 * v + 128) >> 8;
            const int b = (298 * c + 516 * u + 128) >> 8;
            dst[col * 4 + 0] = static_cast<std::uint8_t>(
                r < 0 ? 0 : (r > 255 ? 255 : r));
            dst[col * 4 + 1] = static_cast<std::uint8_t>(
                g < 0 ? 0 : (g > 255 ? 255 : g));
            dst[col * 4 + 2] = static_cast<std::uint8_t>(
                b < 0 ? 0 : (b > 255 ? 255 : b));
            dst[col * 4 + 3] = 255;
        }
    }
    return true;
}

} // namespace
class GstVideoSource final : public VideoSourceBackend {
    using State = VideoSource::State;

public:
    explicit GstVideoSource(VideoSource* host) : VideoSourceBackend(host) {
        std::call_once(s_initFlag, [] { gst_init(nullptr, nullptr); });
    }
    ~GstVideoSource() override { close(); }

    const char* backendName() const override { return "gstreamer"; }
    VideoMetrics metrics() const override {
        VideoMetrics m;
        m.path = m_zeroCopyFrames > 0 ? VideoPath::ZeroCopy
                                      : VideoPath::CpuFallback;
        m.framesDecoded = m_frames.load();
        m.cpuCopies = m_cpuCopies.load();
        m.activeSurfaces = m_inFlight.load();
        return m;
    }

    bool open(const String& uri) override {
        close();
        // playbin needs a URI; accept plain filesystem paths too.
        const std::string text = uri.toStdString();
        gchar* uriOwned = nullptr;
        if (!gst_uri_is_valid(text.c_str()))
            uriOwned = gst_filename_to_uri(text.c_str(), nullptr);
        const std::string finalUri = uriOwned ? std::string(uriOwned) : text;
        g_free(uriOwned);
        if (finalUri.empty()) {
            emitError(String("invalid URI/path: ") + uri);
            return false;
        }

        m_pipeline = gst_element_factory_make("playbin", nullptr);
        if (!m_pipeline) {
            emitError(String("failed to create a playbin"));
            return false;
        }
        g_object_set(m_pipeline, "uri", finalUri.c_str(), nullptr);

        m_sink = gst_element_factory_make("appsink", nullptr);
        // Prefer zero-copy dma-buf NV12; fall back to CPU RGBA when no
        // dma-buf-capable decoder is present in the pipeline.
        GstCaps* caps = zeroCopyEnabled()
            ? gst_caps_from_string("video/x-raw(memory:DMABuf),format=NV12; "
                                   "video/x-raw,format=RGBA")
            : gst_caps_from_string("video/x-raw,format=RGBA");
        // sync=true paces frames against the pipeline clock; max-buffers=1 +
        // drop=true keeps only the newest frame when the consumer (decode
        // conversion, GUI render) is slower than the stream, and qos=true
        // reports the lateness upstream so the decoder skips frames instead
        // of producing them all late — otherwise playback progressively lags
        // behind realtime (visible as slow motion on heavy 4K clips).
        g_object_set(m_sink, "caps", caps, "sync", TRUE, "max-buffers", 1,
                     "drop", TRUE, "qos", TRUE, nullptr);
        gst_caps_unref(caps);
        g_object_set(m_pipeline, "video-sink", m_sink, nullptr);

        // Preroll to make the stream metadata (n-video, duration) available.
        gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
        gst_element_get_state(m_pipeline, nullptr, nullptr, 2 * GST_SECOND);

        gint nVideo = 0, nAudio = 0;
        g_object_get(m_pipeline, "n-video", &nVideo, "n-audio", &nAudio,
                     nullptr);
        m_hasVideo = nVideo > 0;
        m_hasAudio = nAudio > 0;
        if (!m_hasVideo && !m_hasAudio) {
            // Nothing decodable at all (e.g. missing demuxer/decoder
            // plugins): fail so the caller can try another backend.
            emitError(String("no playable streams (decoder plugins "
                          "missing for this container/codec)"));
            close();
            return false;
        }
        if (!m_hasVideo) {
            // No video stream (e.g. music): free the appsink path.
            GstElement* fake = gst_element_factory_make("fakesink", nullptr);
            g_object_set(m_pipeline, "video-sink", fake, nullptr);
            m_sink = nullptr;
        }
        // Stream geometry from the prerolled sink caps, so videoSize() is
        // valid right after open() (matching the ffmpeg backend).
        if (m_sink) {
            GstPad* pad = gst_element_get_static_pad(m_sink, "sink");
            GstCaps* caps = pad ? gst_pad_get_current_caps(pad) : nullptr;
            if (caps) {
                GstVideoInfo info;
                if (gst_video_info_from_caps(&info, caps)) {
                    m_frameWidth = GST_VIDEO_INFO_WIDTH(&info);
                    m_frameHeight = GST_VIDEO_INFO_HEIGHT(&info);
                }
                gst_caps_unref(caps);
            }
            if (pad)
                gst_object_unref(pad);
        }
        m_zeroCopyFrames = 0;
        m_cpuFrames = 0;
        cLogInfo(lcMmGst()) << "open:" << (zeroCopyEnabled() ? "zero-copy caps offered (DMABuf NV12 + RGBA fallback)"
                                                            : "cpu path (FIRMIR_MM_ZERO_COPY=0)");
        m_state = State::Paused;
        emitStateChanged();
        return true;
    }

    void close() override {
        m_pulling = false;
        if (m_pipeline)
            gst_element_set_state(m_pipeline, GST_STATE_NULL); // wakes puller
        if (m_puller.joinable())
            m_puller.join();
        if (m_sink)
            m_sink = nullptr; // owned by the pipeline
        if (m_pipeline) {
            gst_object_unref(m_pipeline);
            m_pipeline = nullptr;
        }
        m_state = State::Stopped;
        m_hasVideo = m_hasAudio = false;
    }

    void play() override {
        if (!m_pipeline || m_state == State::Playing)
            return;
        m_state = State::Playing;
        emitStateChanged();
        gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
        startPulling();
    }

    void pause() override {
        if (!m_pipeline || m_state != State::Playing)
            return;
        m_state = State::Paused;
        emitStateChanged();
        gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
    }

    void stop() override {
        if (!m_pipeline)
            return;
        gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
        seek(0.0);
        m_state = State::Stopped;
        emitStateChanged();
    }

    State state() const override { return m_state; }

    double duration() const override {
        if (!m_pipeline)
            return -1.0;
        gint64 ns = 0;
        if (!gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &ns) ||
            ns < 0)
            return -1.0;
        return static_cast<double>(ns) / GST_SECOND;
    }

    double position() const override {
        if (!m_pipeline)
            return 0.0;
        gint64 ns = 0;
        if (!gst_element_query_position(m_pipeline, GST_FORMAT_TIME, &ns) ||
            ns < 0)
            return 0.0;
        return static_cast<double>(ns) / GST_SECOND;
    }

    void seek(double seconds) override {
        if (!m_pipeline)
            return;
        gst_element_seek_simple(
            m_pipeline, GST_FORMAT_TIME,
            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH |
                                      GST_SEEK_FLAG_KEY_UNIT),
            static_cast<gint64>(seconds * GST_SECOND));
    }

    void videoSize(int& width, int& height) const override {
        width = m_frameWidth;
        height = m_frameHeight;
    }
    bool hasVideo() const override { return m_hasVideo; }
    bool hasAudio() const override { return m_hasAudio; }

private:
    void emitStateChanged() {
        emitState(m_state);
    }

    void startPulling() {
        if (!m_sink || m_puller.joinable())
            return;
        m_pulling = true;
        m_puller = std::thread([this] { pullLoop(); });
    }

    void pullLoop() {
        while (m_pulling) {
            // Blocks until the frame is due (sync=true), paused, EOS or the
            // pipeline is torn down.
            GstSample* sample =
                gst_app_sink_pull_sample(GST_APP_SINK(m_sink));
            if (!sample) {
                if (gst_app_sink_is_eos(GST_APP_SINK(m_sink)))
                    onEos();
                break;
            }
            GstCaps* caps = gst_sample_get_caps(sample);
            GstBuffer* buffer = gst_sample_get_buffer(sample);
            if (caps && buffer) {
                GstStructure* s = gst_caps_get_structure(caps, 0);
                int w = 0, h = 0;
                gst_structure_get_int(s, "width", &w);
                gst_structure_get_int(s, "height", &h);
                const gboolean dmaBuf =
                    gst_buffer_n_memory(buffer) > 0 &&
                    gst_is_dmabuf_memory(gst_buffer_peek_memory(buffer, 0));
                if (w > 0 && h > 0 && dmaBuf)
                    emitGpuFrame(sample, buffer, w, h);
                else if (w > 0 && h > 0)
                    emitCpuFrame(sample, buffer, w, h);
            }
            gst_sample_unref(sample);
        }
    }

    /// Zero-copy path: wrap the dma-buf buffer; no pixel data is touched.
    void emitGpuFrame(GstSample* sample, GstBuffer* buffer, int w, int h) {
        GstVideoMeta* meta = gst_buffer_get_video_meta(buffer);
        const int planes = static_cast<int>(gst_buffer_n_memory(buffer));
        VideoFrame frame;
        frame.width = w;
        frame.height = h;
        const GstClockTime pts = GST_BUFFER_PTS(buffer);
        frame.pts = GST_CLOCK_TIME_IS_VALID(pts)
                        ? static_cast<double>(pts) / GST_SECOND
                        : position();

        gst_sample_ref(sample);
        auto handle = std::make_shared<GstDmaBufFrame>(
            sample, buffer, meta, planes, m_inFlight);
        handle->width = w;
        handle->height = h;
        handle->format = VideoPixelFormat::Nv12;
        handle->pts = frame.pts;
        frame.gpu = std::move(handle);
        // Fallback readback: maps the dma-buf into CPU memory and converts
        // NV12 -> RGBA. Runs only when the renderer cannot import dma-buf.
        frame.readback = [sample2 = gst_sample_ref(sample)](VideoFrame& f) {
            GstBuffer* buf = gst_sample_get_buffer(sample2);
            GstVideoMeta* vmeta = gst_buffer_get_video_meta(buf);
            GstMapInfo mapAll;
            if (!gst_buffer_map(buf, &mapAll, GST_MAP_READ))
                return false;
            const std::uint8_t* base = mapAll.data;
            const std::uint8_t* y = base + (vmeta ? vmeta->offset[0] : 0);
            const std::uint8_t* uv = base + (vmeta ? vmeta->offset[1] : 0);
            const int yStride = vmeta ? vmeta->stride[0] : f.width;
            const int uvStride = vmeta ? vmeta->stride[1] : f.width;
            const bool converted = nv12ToRgba(y, uv, f.width, f.height,
                                              yStride, uvStride, f.rgba);
            gst_buffer_unmap(buf, &mapAll);
            return converted;
        };
        ++m_frames;
        ++m_zeroCopyFrames;
        m_frameWidth = w;
        m_frameHeight = h;
        emitFrame(std::move(frame));
    }

    /// CPU fallback path: the sample (RGBA, as negotiated) is kept alive and
    /// copied only when the frame is actually displayed — the VideoOutput
    /// coalescing drops most frames before they ever need pixels, so a slow
    /// renderer no longer pays a 33 MB copy per dropped 4K frame.
    void emitCpuFrame(GstSample* sample, GstBuffer* buffer, int w, int h) {
        VideoFrame frame;
        frame.width = w;
        frame.height = h;
        const GstClockTime pts = GST_BUFFER_PTS(buffer);
        frame.pts = GST_CLOCK_TIME_IS_VALID(pts)
                        ? static_cast<double>(pts) / GST_SECOND
                        : position();
        auto keep = std::shared_ptr<GstSample>(
            gst_sample_ref(sample),
            [](GstSample* s) { gst_sample_unref(s); });
        frame.readback = [this, keep](VideoFrame& f) {
            GstBuffer* buf = gst_sample_get_buffer(keep.get());
            GstMapInfo map;
            if (!gst_buffer_map(buf, &map, GST_MAP_READ))
                return false;
            f.rgba.assign(map.data, map.data + map.size);
            gst_buffer_unmap(buf, &map);
            ++m_cpuCopies;
            return true;
        };
        ++m_frames;
        ++m_cpuFrames;
        m_frameWidth = w;
        m_frameHeight = h;
        emitFrame(std::move(frame));
    }

    void onEos() {
        // Rewind and park in the paused state so play() restarts the clip.
        if (m_pipeline) {
            gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
            seek(0.0);
        }
        m_state = State::Stopped;
        emitStateChanged();
    }

    GstElement* m_pipeline = nullptr;
    GstElement* m_sink = nullptr; // owned by the pipeline
    std::thread m_puller;
    std::atomic<bool> m_pulling{false};
    State m_state = State::Stopped;
    bool m_hasVideo = false;
    bool m_hasAudio = false;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
    // Metrics: emitted frames by path and live dma-buf handles.
    std::atomic<std::uint64_t> m_frames{0};
    std::atomic<std::uint64_t> m_zeroCopyFrames{0};
    std::atomic<std::uint64_t> m_cpuFrames{0};
    /// Actual pixel copies (lazy readbacks of displayed frames).
    std::atomic<std::uint64_t> m_cpuCopies{0};
    std::atomic<int> m_inFlight{0};

    static std::once_flag s_initFlag;
};

std::once_flag GstVideoSource::s_initFlag;

/// Plugin entry (firmir-mm-gstreamer.so).
const MmSourcePlugin* gstPlugin() {
    static const MmSourcePlugin plugin = {
        FIRMIR_MM_PLUGIN_ABI, "gstreamer",
        []() -> bool {
            GError* err = nullptr;
            const gboolean ok = gst_init_check(nullptr, nullptr, &err);
            if (err)
                g_error_free(err);
            return ok == TRUE;
        },
        [](VideoSource* host) -> VideoSourceBackend* {
            return new GstVideoSource(host);
        }};
    return &plugin;
}

} // namespace Multimedia
} // namespace Firmir

extern "C" const Firmir::Multimedia::MmSourcePlugin* firmir_mm_plugin() {
    return Firmir::Multimedia::gstPlugin();
}

