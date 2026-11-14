// wl_shm buffer backend: reads client buffers straight from the shared
// mapping. No GPU, no EGL — this is the reader every other backend falls back
// to for CPU-drawn clients.

#include <firmir/wayland/compositor/bufferbackend.hpp>

#include <wayland-server.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace {

using Firmir::Wayland::Compositor::BufferBackend;
using Firmir::Wayland::Compositor::ImportedBuffer;
using Firmir::Wayland::Compositor::WaylandBufferRef;

class ShmImportedBuffer final : public ImportedBuffer {
public:
    explicit ShmImportedBuffer(const WaylandBufferRef& buffer) {
        wl_shm_buffer* shm = wl_shm_buffer_get(buffer.handle());
        if (!shm)
            return;
        mBuffer = shm;
        mWidth = wl_shm_buffer_get_width(shm);
        mHeight = wl_shm_buffer_get_height(shm);
        const int stride = wl_shm_buffer_get_stride(shm);
        const std::uint32_t format = wl_shm_buffer_get_format(shm);
        if (mWidth <= 0 || mHeight <= 0 || stride < mWidth * 4)
            return;
        if (format != WL_SHM_FORMAT_ARGB8888 && format != WL_SHM_FORMAT_XRGB8888) {
            std::fprintf(stderr,
                         "firmir-wayland-compositor: shm: unsupported format "
                         "0x%08x\n",
                         format);
            return;
        }
        wl_shm_buffer_begin_access(shm);
        mAccess = true;
        const auto* data =
            static_cast<const std::uint8_t*>(wl_shm_buffer_get_data(shm));
        if (!data)
            return;
        mPixels.resize(static_cast<std::size_t>(mWidth) *
                       static_cast<std::size_t>(mHeight));
        for (int y = 0; y < mHeight; ++y)
            std::memcpy(mPixels.data() + static_cast<std::size_t>(y) * mWidth,
                        data + static_cast<std::size_t>(y) * stride,
                        static_cast<std::size_t>(mWidth) * 4);
        // An XRGB client does not have to fill the alpha channel; the painter
        // blends with it, so make the frame opaque like every other backend.
        if (format == WL_SHM_FORMAT_XRGB8888)
            for (std::uint32_t& pixel : mPixels)
                pixel |= 0xff000000u;
        mValid = true;
    }

    ~ShmImportedBuffer() override {
        if (mAccess)
            wl_shm_buffer_end_access(mBuffer);
    }

    bool valid() const override { return mValid; }
    int width() const override { return mWidth; }
    int height() const override { return mHeight; }
    std::vector<std::uint32_t>& pixels() override { return mPixels; }

private:
    wl_shm_buffer* mBuffer = nullptr;
    std::vector<std::uint32_t> mPixels;
    int mWidth = 0;
    int mHeight = 0;
    bool mAccess = false;
    bool mValid = false;
};

class ShmBackend final : public BufferBackend {
public:
    const char* name() const override { return "shm"; }

    std::unique_ptr<ImportedBuffer> import(
        const WaylandBufferRef& buffer) override {
        if (!buffer.isShm())
            return nullptr; // not a wl_shm buffer: a GPU backend has to read it
        return std::make_unique<ShmImportedBuffer>(buffer);
    }
};

} // namespace

extern "C" Firmir::Wayland::Compositor::BufferBackend*
firmir_wayland_compositor_create_backend(wl_display* display) {
    (void)display; // wl_shm needs nothing from the display
    return new ShmBackend();
}
