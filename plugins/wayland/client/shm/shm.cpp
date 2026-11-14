// wl_shm client backend: the portable frame-buffer plugin.
//
// Every buffer is an anonymous shared-memory mapping handed to the compositor
// as a wl_shm buffer in ARGB8888. The compositor therefore needs no GPU and no
// buffer-sharing protocol, at the price of a CPU copy in both directions.

#include <firmir/wayland/client/backend.hpp>

#include <wayland-client.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace {

std::atomic<unsigned> gFileCounter{0};

/// Anonymous shared-memory file of @p bytes, already unlinked (-1 on failure).
int createAnonymousFile(std::size_t bytes) {
    char name[64];
    for (int attempt = 0; attempt < 64; ++attempt) {
        std::snprintf(name, sizeof(name), "/firmir-wl-%d-%u",
                      static_cast<int>(::getpid()), gFileCounter.fetch_add(1));
        const int fd = ::shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd < 0) {
            if (errno == EEXIST)
                continue; // another process took the name first
            return -1;
        }
        ::shm_unlink(name); // anonymous from here on; the fd stays valid
        if (::ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
            ::close(fd);
            return -1;
        }
        return fd;
    }
    return -1;
}

class ShmBuffer final : public Firmir::Wayland::Client::BackendBuffer {
public:
    ShmBuffer(wl_shm* shm, int width, int height);
    ~ShmBuffer() override;

    wl_buffer* handle() const override { return mBuffer; }
    int width() const override { return mWidth; }
    int height() const override { return mHeight; }
    int stride() const override { return mWidth * 4; }
    std::uint32_t* pixels() const override { return mPixels; }
    std::size_t bytes() const override { return mBytes; }
    bool valid() const { return mBuffer && mPixels; }

private:
    wl_buffer* mBuffer = nullptr;
    std::uint32_t* mPixels = nullptr;
    std::size_t mBytes = 0;
    int mFd = -1;
    int mWidth = 0;
    int mHeight = 0;
};

ShmBuffer::ShmBuffer(wl_shm* shm, int width, int height)
    : mWidth(width), mHeight(height) {
    if (!shm || width <= 0 || height <= 0)
        return;
    mBytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    mFd = createAnonymousFile(mBytes);
    if (mFd < 0) {
        std::fprintf(stderr, "firmir-wayland-client: shm: shm_open failed\n");
        return;
    }
    void* data =
        ::mmap(nullptr, mBytes, PROT_READ | PROT_WRITE, MAP_SHARED, mFd, 0);
    if (data == MAP_FAILED) {
        std::fprintf(stderr, "firmir-wayland-client: shm: mmap failed\n");
        ::close(mFd);
        mFd = -1;
        return;
    }
    mPixels = static_cast<std::uint32_t*>(data);
    wl_shm_pool* pool =
        wl_shm_create_pool(shm, mFd, static_cast<int>(mBytes));
    if (!pool) {
        std::fprintf(stderr, "firmir-wayland-client: shm: pool creation failed\n");
        return;
    }
    mBuffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4,
                                        WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool); // the buffer keeps the memory alive
}

ShmBuffer::~ShmBuffer() {
    if (mBuffer)
        wl_buffer_destroy(mBuffer);
    if (mPixels)
        ::munmap(mPixels, mBytes);
    if (mFd >= 0)
        ::close(mFd);
}

class ShmBackend final : public Firmir::Wayland::Client::Backend {
public:
    explicit ShmBackend(wl_shm* shm) : mShm(shm) {}

    const char* name() const override { return "shm"; }

    std::unique_ptr<Firmir::Wayland::Client::BackendBuffer> createBuffer(
        int width, int height) override {
        auto buffer = std::make_unique<ShmBuffer>(mShm, width, height);
        if (!buffer->valid())
            return nullptr;
        return buffer;
    }

private:
    wl_shm* mShm = nullptr;
};

} // namespace

extern "C" Firmir::Wayland::Client::Backend*
firmir_wayland_client_create_backend(
    const Firmir::Wayland::Client::BackendParams& params) {
    // wl_shm is the only requirement; without the global there is nothing to
    // allocate from and the loader moves on to the next backend.
    if (!params.display || !params.shm) {
        std::fprintf(stderr,
                     "firmir-wayland-client: shm: the compositor advertises no "
                     "wl_shm global\n");
        return nullptr;
    }
    return new ShmBackend(params.shm);
}
