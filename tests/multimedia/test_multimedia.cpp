// firmir-multimedia: backend reporting/factory, the VideoSource contract on a
// mock, and VideoOutput letterboxed rendering of a queued frame (soft
// rasterizer).
#include "test_framework.hpp"

#include <firmir/multimedia/videooutput.hpp>
#include <firmir/multimedia/videosource.hpp>
#include <firmir/multimedia/videosourcebackend.hpp>
#include <firmir/object.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#ifdef FIRMIR_RENDER_TEST_SOFT
#include <firmir/gui/painter.hpp>
#include <firmir/render/render.hpp>
#endif

#include <chrono>
#include <cstdlib>
#include <memory>

using namespace std::chrono_literals;

namespace {

/// In-memory decode backend: play/pause/stop just switch the state (emitting
/// stateChanged); pushFrame() emits a solid-color frame like a decoder would.
class MockBackend : public Firmir::Multimedia::VideoSourceBackend {
public:
    using VideoSourceBackend::VideoSourceBackend;
    using State = Firmir::Multimedia::VideoSource::State;

    bool open(const Firmir::String& uri) override {
        m_uri = uri;
        return !m_uri.toStdString().empty();
    }
    void close() override {}
    void play() override { setState(State::Playing); }
    void pause() override { setState(State::Paused); }
    void stop() override {
        m_position = 0.0;
        setState(State::Stopped);
    }
    State state() const override { return m_state; }
    double duration() const override { return 12.5; }
    double position() const override { return m_position; }
    void seek(double seconds) override { m_position = seconds; }
    void videoSize(int& width, int& height) const override {
        width = m_width;
        height = m_height;
    }
    bool hasVideo() const override { return true; }
    bool hasAudio() const override { return false; }
    const char* backendName() const override { return "mock"; }

    /// Emits a fully prepared frame (CPU or GPU-carrying) like a decoder.
    void pushVideoFrame(Firmir::Multimedia::VideoFrame frame) {
        emitFrame(std::move(frame));
    }

    void pushFrame(int w, int h, std::uint8_t r, std::uint8_t g,
                   std::uint8_t b) {
        Firmir::Multimedia::VideoFrame frame;
        frame.width = w;
        frame.height = h;
        frame.pts = m_position;
        frame.rgba.resize(static_cast<size_t>(w) * h * 4);
        for (size_t i = 0; i < frame.rgba.size(); i += 4) {
            frame.rgba[i] = r;
            frame.rgba[i + 1] = g;
            frame.rgba[i + 2] = b;
            frame.rgba[i + 3] = 255;
        }
        m_width = w;
        m_height = h;
        emitFrame(std::move(frame));
    }

private:
    void setState(State state) {
        m_state = state;
        emitState(state);
    }

    Firmir::String m_uri;
    State m_state = State::Stopped;
    double m_position = 0.0;
    int m_width = 0;
    int m_height = 0;
};

/// A VideoSource running a MockBackend; exposes the backend for pushFrame().
struct MockVideoSource {
    MockVideoSource() {
        auto backend = std::make_unique<MockBackend>();
        mock = backend.get();
        source = std::make_unique<Firmir::Multimedia::VideoSource>(
            std::move(backend));
    }
    Firmir::Multimedia::VideoSource& get() { return *source; }

    std::unique_ptr<Firmir::Multimedia::VideoSource> source;
    MockBackend* mock = nullptr;
};

} // namespace

TEST_CASE(multimedia_backend_factory) {
    using namespace Firmir::Multimedia;
#ifdef FIRMIR_MM_TEST_PLUGIN_DIR
    ::setenv("FIRMIR_MULTIMEDIA_PLUGIN_PATH", FIRMIR_MM_TEST_PLUGIN_DIR, 1);
#endif
    CHECK(createVideoSource("definitely-not-a-backend") == nullptr);
    const auto backends = availableVideoSourceBackends();
    if (backends.empty()) {
        // No gstreamer/ffmpeg dev packages: nothing to create.
        CHECK(createVideoSource("auto") == nullptr);
    } else {
        std::unique_ptr<VideoSource> source(createVideoSource("auto"));
        CHECK(source != nullptr);
        if (source)
            CHECK(std::string(source->backendName()) == backends.front());
    }
}

TEST_CASE(multimedia_source_contract) {
    MockVideoSource source;
    int lastState = -1;
    source.get().connect<int>(&source.get(), Firmir::Multimedia::VideoSource::signalStateChanged,
                        [&lastState](int state) { lastState = state; });

    CHECK(source.get().state() == Firmir::Multimedia::VideoSource::State::Stopped);
    CHECK(source.get().open("file:///clip.mp4"));
    CHECK(!source.get().open(""));

    source.get().play();
    CHECK(source.get().state() == Firmir::Multimedia::VideoSource::State::Playing);
    CHECK(lastState == static_cast<int>(Firmir::Multimedia::VideoSource::State::Playing));

    source.get().seek(4.25);
    CHECK_EQ(source.get().position(), 4.25);
    CHECK_EQ(source.get().duration(), 12.5);

    source.get().pause();
    CHECK(lastState == static_cast<int>(Firmir::Multimedia::VideoSource::State::Paused));
    source.get().stop();
    CHECK(source.get().state() == Firmir::Multimedia::VideoSource::State::Stopped);
    CHECK_EQ(source.get().position(), 0.0);
}

TEST_CASE(multimedia_video_output_receives_queued_frame) {
    MockVideoSource source;
    Firmir::Multimedia::VideoOutput output;
    output.setSource(&source.get());

    // Watch the effect of the queued frameReady through a side connection.
    int frames = 0;
    source.get().connect<Firmir::Multimedia::VideoFrame>(
        &output, Firmir::Multimedia::VideoSource::signalFrameReady,
        [&frames](const Firmir::Multimedia::VideoFrame&) { ++frames; },
        Firmir::ConnectionMode::Queued);

    source.mock->pushFrame(4, 2, 255, 0, 0);
    CHECK_EQ(frames, 0); // queued: not delivered before the loop runs

    Firmir::Object ctx;
    Firmir::Timer::singleShot(20ms, &ctx,
                              [] { Firmir::Thread::mainThread()->quit(); });
    Firmir::Thread::mainThread()->exec();
    CHECK_EQ(frames, 1);

    // Detaching and re-attaching the source must not crash or double-connect.
    output.setSource(nullptr);
    output.setSource(&source.get());
    source.mock->pushFrame(2, 2, 0, 255, 0);
    Firmir::Timer::singleShot(20ms, &ctx,
                              [] { Firmir::Thread::mainThread()->quit(); });
    Firmir::Thread::mainThread()->exec();
    CHECK_EQ(frames, 2);
}

#ifdef FIRMIR_RENDER_TEST_SOFT
TEST_CASE(multimedia_video_output_renders_letterboxed_frame) {
#ifdef FIRMIR_RENDER_TEST_PLUGIN_DIR
    ::setenv("FIRMIR_RENDER_PLUGIN_PATH", FIRMIR_RENDER_TEST_PLUGIN_DIR, 1);
#endif
    auto device = Firmir::Render::Render::createDevice("soft");
    CHECK(device != nullptr);
    if (!device)
        return;

    MockVideoSource source;
    struct TestOutput : Firmir::Multimedia::VideoOutput {
        using Firmir::Multimedia::VideoOutput::paint;
    } output;
    output.setGeometry(0, 0, 100, 100);
    output.setSource(&source.get());
    source.mock->pushFrame(4, 2, 255, 0, 0); // 4x2 red -> 100x50 letterboxed

    Firmir::Object ctx;
    Firmir::Timer::singleShot(20ms, &ctx,
                              [] { Firmir::Thread::mainThread()->quit(); });
    Firmir::Thread::mainThread()->exec(); // deliver the queued frame

    Firmir::Gui::RenderResources resources(device.get());
    std::unique_ptr<Firmir::Render::RenderTarget> target(
        device->createRenderTarget(
            {100, 100, Firmir::Render::TextureFormat::Rgba8, false}));
    std::unique_ptr<Firmir::Render::RenderCommandBuffer> commands(
        device->createCommandBuffer());
    CHECK(target && commands);
    if (!target || !commands)
        return;

    commands->begin();
    Firmir::Render::ClearValue clear; // opaque black
    clear.a = 1;
    commands->beginRenderPass(target.get(), clear);
    {
        Firmir::Gui::Painter painter(resources, commands.get(), target.get());
        output.paint(painter);
    }
    commands->endRenderPass();
    commands->end();
    device->submit(commands.get());

    const auto* cpu =
        dynamic_cast<Firmir::Render::CpuRenderTarget*>(target.get());
    CHECK(cpu != nullptr);
    if (!cpu)
        return;
    const std::uint32_t* px = cpu->pixels(); // 0xAARRGGBB
    CHECK_EQ(px[0 * 100 + 50], 0xFF000000u);  // top letterbox bar
    CHECK_EQ(px[50 * 100 + 50], 0xFFFF0000u); // red frame center
    CHECK_EQ(px[99 * 100 + 50], 0xFF000000u); // bottom letterbox bar
}
#endif // FIRMIR_RENDER_TEST_SOFT

namespace {

/// Minimal in-memory GPU frame: reports a dma-buf descriptor with an invalid
/// fd, so renderers always refuse to import it (fallback-friendly).
class FakeGpuFrame : public Firmir::Multimedia::GpuFrameHandle {
public:
    Backend backend() const override { return Backend::DmaBuf; }
    int planeCount() const override { return 2; }
    int planeFd(int) const override { return -1; }
    std::uint32_t planeOffset(int) const override { return 0; }
    std::uint32_t planeStride(int) const override { return 0; }
    std::uint32_t drmFormat() const override { return 0x3231564E; } // NV12
    std::uint64_t drmModifier() const override { return 0x00FFFFFFFFFFFFFFULL; }
    std::uintptr_t poolKey() const override { return 0xF00D; }
};

} // namespace

TEST_CASE(multimedia_frame_pool_lifecycle) {
    using namespace Firmir::Multimedia;
    int allocated = 0;
    VideoFramePool pool(3, [&allocated](void*) {
        ++allocated;
        return std::shared_ptr<GpuFrameHandle>(new FakeGpuFrame());
    });
    CHECK_EQ(pool.capacity(), 3);
    CHECK_EQ(pool.activeCount(), 0);
    {
        auto a = pool.acquire();
        auto b = pool.acquire();
        CHECK(a && b);
        CHECK(a.get() != b.get());
        CHECK_EQ(pool.activeCount(), 2);
        {
            auto c = pool.acquire();
            CHECK(c != nullptr);
            CHECK_EQ(pool.activeCount(), 3);
            // Exhausted: non-blocking miss, drop counter ticks.
            CHECK(pool.acquire() == nullptr);
            CHECK_EQ(pool.droppedAcquires(), 1);
            CHECK_EQ(allocated, 3);
        }
        CHECK_EQ(pool.activeCount(), 2); // c returned to the pool
    }
    CHECK_EQ(pool.activeCount(), 0);
    CHECK_EQ(pool.acquireCount(), 3);
    // Re-acquire reuses a slot without new allocations by the caller.
    auto d = pool.acquire();
    CHECK(d != nullptr);
    CHECK_EQ(allocated, 4); // backend allocator ran again for the slot
    CHECK_EQ(pool.activeCount(), 1);
}

TEST_CASE(multimedia_gpu_frame_readback_fallback) {
    // A GPU frame whose renderer import fails must transparently read back
    // through the backend-provided functor (no caller changes).
    Firmir::Multimedia::VideoFrame frame;
    frame.width = 4;
    frame.height = 2;
    frame.gpu.reset(new FakeGpuFrame());
    int readbacks = 0;
    frame.readback = [&readbacks](Firmir::Multimedia::VideoFrame& f) {
        ++readbacks;
        f.rgba.assign(size_t(4) * 2 * 4, 255);
        return true;
    };
    CHECK(frame.isValid());
    CHECK(frame.hasGpuPixels());
    CHECK(frame.ensureCpuPixels());
    CHECK_EQ(frame.rgba.size(), size_t(4) * 2 * 4);
    CHECK_EQ(readbacks, 1);
    CHECK(frame.ensureCpuPixels()); // cached: no second readback
    CHECK_EQ(readbacks, 1);
}

#ifdef FIRMIR_RENDER_TEST_SOFT
TEST_CASE(multimedia_video_output_fill_modes) {
    // 4x2 red frame in a 100x100 output: Fit letterboxes to 100x50 centered,
    // Pad draws 1:1 centered, Crop covers the whole area (clipped).
#ifdef FIRMIR_RENDER_TEST_PLUGIN_DIR
    ::setenv("FIRMIR_RENDER_PLUGIN_PATH", FIRMIR_RENDER_TEST_PLUGIN_DIR, 1);
#endif
    auto device = Firmir::Render::Render::createDevice("soft");
    CHECK(device != nullptr);
    if (!device)
        return;

    using FillMode = Firmir::Multimedia::VideoOutput::FillMode;
    const FillMode modes[] = {FillMode::Stretch, FillMode::PreserveAspectFit,
                              FillMode::PreserveAspectCrop, FillMode::Pad};
    for (FillMode mode : modes) {
        MockVideoSource source;
        struct TestOutput : Firmir::Multimedia::VideoOutput {
            using Firmir::Multimedia::VideoOutput::paint;
        } output;
        output.setGeometry(0, 0, 100, 100);
        output.setFillMode(mode);
        output.setSource(&source.get());
        source.mock->pushFrame(4, 2, 255, 0, 0);

        Firmir::Object ctx;
        Firmir::Timer::singleShot(20ms, &ctx,
                                  [] { Firmir::Thread::mainThread()->quit(); });
        Firmir::Thread::mainThread()->exec();

        Firmir::Gui::RenderResources resources(device.get());
        std::unique_ptr<Firmir::Render::RenderTarget> target(
            device->createRenderTarget(
                {100, 100, Firmir::Render::TextureFormat::Rgba8, false}));
        std::unique_ptr<Firmir::Render::RenderCommandBuffer> commands(
            device->createCommandBuffer());
        commands->begin();
        Firmir::Render::ClearValue clear;
        clear.a = 1;
        commands->beginRenderPass(target.get(), clear);
        {
            Firmir::Gui::Painter painter(resources, commands.get(), target.get());
            output.paint(painter);
        }
        commands->endRenderPass();
        commands->end();
        device->submit(commands.get());

        const auto* cpu =
            dynamic_cast<Firmir::Render::CpuRenderTarget*>(target.get());
        CHECK(cpu != nullptr);
        if (!cpu)
            continue;
        const std::uint32_t* px = cpu->pixels();
        const auto red = [&px](int x, int y) {
            return px[y * 100 + x] == 0xFFFF0000u;
        };
        if (mode == FillMode::Pad) {
            // 1:1 centered: the 4x2 frame sits at (48,49)-(51,50).
            CHECK(red(50, 49));
            CHECK(!red(0, 0));
            CHECK(!red(50, 60));
        } else if (mode == FillMode::PreserveAspectFit) {
            // 100x50 letterbox centered vertically.
            CHECK(red(50, 50));
            CHECK(!red(50, 10));
            CHECK(red(50, 74));
        } else {
            // Stretch / Crop cover the whole area.
            CHECK(red(0, 0));
            CHECK(red(99, 99));
            CHECK(red(50, 50));
        }
    }
}

TEST_CASE(multimedia_video_output_gpu_frame_soft_fallback) {
    // Soft renderer cannot import dma-buf: a GPU frame falls back to the
    // readback + CPU draw path and is counted as a fallback frame.
#ifdef FIRMIR_RENDER_TEST_PLUGIN_DIR
    ::setenv("FIRMIR_RENDER_PLUGIN_PATH", FIRMIR_RENDER_TEST_PLUGIN_DIR, 1);
#endif
    auto device = Firmir::Render::Render::createDevice("soft");
    CHECK(device != nullptr);
    if (!device)
        return;
    CHECK(!device->supportsDmaBufTextures());

    MockVideoSource source;
    struct TestOutput : Firmir::Multimedia::VideoOutput {
        using Firmir::Multimedia::VideoOutput::paint;
    } output;
    output.setGeometry(0, 0, 100, 100);
    output.setSource(&source.get());

    // GPU frame with an unreadable descriptor + working readback (solid red).
    Firmir::Multimedia::VideoFrame frame;
    frame.width = 4;
    frame.height = 2;
    frame.pts = 0.0;
    frame.gpu.reset(new FakeGpuFrame());
    frame.readback = [](Firmir::Multimedia::VideoFrame& f) {
        f.rgba.resize(size_t(f.width) * f.height * 4);
        for (size_t i = 0; i < f.rgba.size(); i += 4) {
            f.rgba[i] = 255;
            f.rgba[i + 1] = 0;
            f.rgba[i + 2] = 0;
            f.rgba[i + 3] = 255;
        }
        return true;
    };
    source.mock->pushVideoFrame(std::move(frame));

    Firmir::Object ctx;
    Firmir::Timer::singleShot(20ms, &ctx,
                              [] { Firmir::Thread::mainThread()->quit(); });
    Firmir::Thread::mainThread()->exec();

    Firmir::Gui::RenderResources resources(device.get());
    std::unique_ptr<Firmir::Render::RenderTarget> target(
        device->createRenderTarget(
            {100, 100, Firmir::Render::TextureFormat::Rgba8, false}));
    std::unique_ptr<Firmir::Render::RenderCommandBuffer> commands(
        device->createCommandBuffer());
    CHECK(target && commands);
    if (!target || !commands)
        return;
    commands->begin();
    Firmir::Render::ClearValue clear;
    clear.a = 1;
    commands->beginRenderPass(target.get(), clear);
    {
        Firmir::Gui::Painter painter(resources, commands.get(), target.get());
        output.paint(painter);
    }
    commands->endRenderPass();
    commands->end();
    device->submit(commands.get());

    const auto* cpu =
        dynamic_cast<Firmir::Render::CpuRenderTarget*>(target.get());
    CHECK(cpu != nullptr);
    if (!cpu)
        return;
    const std::uint32_t* px = cpu->pixels();
    CHECK_EQ(px[50 * 100 + 50], 0xFFFF0000u); // readback frame rendered
    CHECK_EQ(output.zeroCopyFrames(), 0);
    CHECK_EQ(output.cpuFallbackFrames(), 1);
}
#endif // FIRMIR_RENDER_TEST_SOFT
