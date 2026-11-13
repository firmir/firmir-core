// firmir-multimedia backend e2e (ffmpeg): synthesizes a 1-second rawvideo
// AVI with libavformat (no encoder needed — raw packets), then plays it
// through the ffmpeg VideoSource backend and checks the decoded frames.
//
// Checks: open() metadata, frame size/content, pts monotony, ~realtime
// pacing, position tracking, seek.

#include <firmir/multimedia/videosource.hpp>
#include <firmir/object.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {
using namespace std::chrono_literals;
constexpr int kWidth = 64;
constexpr int kHeight = 48;
constexpr int kFrames = 10; // 10 fps -> 1 second

#define CHECK(x)                                                               \
    do {                                                                       \
        if (!(x)) {                                                            \
            std::fprintf(stderr, "mediatest: CHECK failed at line %d\n",       \
                         __LINE__);                                            \
            return 1;                                                          \
        }                                                                      \
    } while (false)

/// Writes a rawvideo (BGR24) AVI with kFrames solid-color frames.
bool synthesizeAvi(const char* path) {
    AVFormatContext* oc = nullptr;
    if (avformat_alloc_output_context2(&oc, nullptr, "avi", path) < 0 || !oc)
        return false;
    AVStream* stream = avformat_new_stream(oc, nullptr);
    if (!stream)
        return false;
    stream->time_base = {1, kFrames};
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->codec_id = AV_CODEC_ID_RAWVIDEO;
    stream->codecpar->width = kWidth;
    stream->codecpar->height = kHeight;
    stream->codecpar->format = AV_PIX_FMT_BGR24;
    stream->codecpar->codec_tag = 0; // BI_RGB
    stream->codecpar->bits_per_coded_sample = 24;
    if (avio_open(&oc->pb, path, AVIO_FLAG_WRITE) < 0)
        return false;
    if (avformat_write_header(oc, nullptr) < 0)
        return false;

    std::unique_ptr<std::uint8_t[]> pixels(new std::uint8_t[kWidth * kHeight * 3]);
    for (int i = 0; i < kFrames; ++i) {
        // Solid frames with a moving gradient so frames differ.
        for (int p = 0; p < kWidth * kHeight; ++p) {
            pixels[p * 3 + 0] = static_cast<std::uint8_t>(16 * i);       // B
            pixels[p * 3 + 1] = 0;                                       // G
            pixels[p * 3 + 2] = static_cast<std::uint8_t>(200 - 16 * i); // R
        }
        AVPacket packet;
        av_new_packet(&packet, kWidth * kHeight * 3);
        std::memcpy(packet.data, pixels.get(), kWidth * kHeight * 3);
        packet.stream_index = stream->index;
        packet.pts = packet.dts = i;
        packet.duration = 1;
        if (av_interleaved_write_frame(oc, &packet) < 0)
            return false;
    }
    av_write_trailer(oc);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    return true;
}
} // namespace

int main() {
#ifdef FIRMIR_MM_TEST_PLUGIN_DIR
#ifdef _WIN32
    ::_putenv_s("FIRMIR_MULTIMEDIA_PLUGIN_PATH", FIRMIR_MM_TEST_PLUGIN_DIR);
#else
    ::setenv("FIRMIR_MULTIMEDIA_PLUGIN_PATH", FIRMIR_MM_TEST_PLUGIN_DIR, 1);
#endif
#endif
    const char* path = "/tmp/firmir_multimedia_mediatest.avi";
    if (!synthesizeAvi(path)) {
        std::fprintf(stderr, "mediatest: cannot synthesize %s\n", path);
        return 1;
    }
    Firmir::Object root;
    std::unique_ptr<Firmir::Multimedia::VideoSource> source(
        Firmir::Multimedia::createVideoSource("ffmpeg", &root));
    CHECK(source != nullptr);
    CHECK(std::string(source->backendName()) == "ffmpeg");
    CHECK(source->open(Firmir::String(path)));
    CHECK(source->hasVideo());
    int w = 0, h = 0;
    source->videoSize(w, h);
    CHECK(w == kWidth && h == kHeight);
    const double duration = source->duration();
    CHECK(duration > 0.8 && duration < 1.3); // ~1 s

    int frames = 0;
    double lastPts = -1.0;
    bool ptsMonotone = true;
    bool sizesOk = true;
    source->connect<Firmir::Multimedia::VideoFrame>(
        &root, Firmir::Multimedia::VideoSource::signalFrameReady,
        [&](const Firmir::Multimedia::VideoFrame& frame) {
            ++frames;
            if (frame.width != kWidth || frame.height != kHeight ||
                frame.rgba.size() != size_t(kWidth) * kHeight * 4)
                sizesOk = false;
            if (frame.pts < lastPts)
                ptsMonotone = false;
            lastPts = frame.pts;
        },
        Firmir::ConnectionMode::Queued);
    bool sawStopped = false;
    source->connect<int>(
        &root, Firmir::Multimedia::VideoSource::signalStateChanged,
        [&](int state) {
            if (state == static_cast<int>(
                    Firmir::Multimedia::VideoSource::State::Stopped))
                sawStopped = true;
        },
        Firmir::ConnectionMode::Queued);

    // Bail out after 10 s no matter what; quit as soon as the stream ends.
    Firmir::Timer::singleShot(10s, &root,
                              [] { Firmir::Thread::mainThread()->quit(); });
    Firmir::Timer watchdog(&root);
    watchdog.setInterval(100ms);
    watchdog.connect(&watchdog, "timeout", [&] {
        if (sawStopped)
            Firmir::Thread::mainThread()->quit();
    });
    watchdog.start();

    const auto started = std::chrono::steady_clock::now();
    source->play();
    Firmir::Thread::mainThread()->exec();
    const double elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - started).count();

    CHECK(sawStopped);
    CHECK(frames >= kFrames - 2); // a tail frame may race with teardown
    CHECK(sizesOk);
    CHECK(ptsMonotone);
    CHECK(lastPts > 0.5); // pts actually advanced
    CHECK(elapsed > 0.5); // frames were paced, not dumped at once
    CHECK(source->position() > 0.5 && source->position() <= duration + 0.2);

    // Seek back and replay a moment.
    frames = 0;
    lastPts = 0.0;
    sawStopped = false;
    source->seek(0.3);
    source->play();
    Firmir::Timer::singleShot(1500ms, &root,
                              [] { Firmir::Thread::mainThread()->quit(); });
    Firmir::Thread::mainThread()->exec();
    source->stop();
    CHECK(frames >= 3); // ~7 frames from 0.3 s to EOF within the 1.5 s window
    CHECK(lastPts >= 0.15); // resumed near the seek point (keyframe rounding)

    // Pipeline observability: a path is selected, decode time is accounted,
    // no frames are dropped on the CPU path.
    const auto metrics = source->metrics();
    CHECK(metrics.path != Firmir::Multimedia::VideoPath::Unknown);
    CHECK(metrics.framesDecoded > 0);
    CHECK(metrics.decodeTimeUs > 0);
    CHECK(metrics.droppedFrames == 0);
    std::printf("mediametrics: path=%s decoded=%llu cpuCopies=%llu "
                "decodeMs=%.1f poolWaitUs=%llu drops=%llu\n",
                metrics.path == Firmir::Multimedia::VideoPath::ZeroCopy ? "zero-copy" : "cpu",
                (unsigned long long)metrics.framesDecoded,
                (unsigned long long)metrics.cpuCopies,
                metrics.decodeTimeUs / 1000.0,
                (unsigned long long)metrics.poolWaitUs,
                (unsigned long long)metrics.droppedFrames);

    std::printf("mediatest: OK (duration %.2f s, last pts %.2f)\n", duration,
                lastPts);
    return 0;
}