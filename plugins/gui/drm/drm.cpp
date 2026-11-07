// DRM/KMS display backend for firmir-gui: raw kernel ioctls only (no libdrm
// dependency). Each connected connector with a usable CRTC becomes a screen;
// a window presented on it is rendered into a dumb buffer and modeset to the
// CRTC. Screen hotplug is detected by polling the connector status.
//
// Requires DRM master (typically: run on a free VT, or with CAP_SYS_ADMIN).
// Device node: FIRMIR_DRM_DEVICE (default /dev/dri/card0).

#include <firmir/gui/backend.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <fcntl.h>
#include <memory>
#include <mutex>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <thread>
#include <unistd.h>
#include <vector>

// Older kernel headers lack enum drm_connector_status.
#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#define DRM_MODE_DISCONNECTED 2
#define DRM_MODE_UNKNOWNCONNECTION 3
#endif

using namespace Firmir::Gui;

namespace {

int drmIoctl(int fd, unsigned long request, void* arg) {
    int ret;
    do {
        ret = ::ioctl(fd, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
    return ret;
}

/// A connected connector with its chosen mode and usable CRTC.
struct DrmScreenInfo {
    std::uint32_t connectorId = 0;
    std::uint32_t crtcId = 0;
    std::uint32_t encoderId = 0;
    drm_mode_modeinfo mode{};
    std::string name;
    // dumb buffer + FB for the single fullscreen surface
    std::uint32_t fbId = 0;
    std::uint32_t handle = 0;
    std::uint64_t mapSize = 0;
    std::uint32_t pitch = 0;
    void* map = nullptr;
    bool modesetDone = false;
    drm_mode_crtc savedCrtc{};
};

class DrmDisplay;

class DrmScreen : public PlatformScreen {
public:
    DrmScreen(DrmScreenInfo info) : m_info(std::move(info)) {}
    std::string name() const override { return m_info.name; }
    Size size() const override {
        return {m_info.mode.hdisplay, m_info.mode.vdisplay};
    }
    DrmScreenInfo m_info;
};

class DrmWindow : public PlatformWindow {
public:
    DrmWindow(int fd, DrmScreen* screen) : m_fd(fd), m_screen(screen) {}
    ~DrmWindow() override {
        if (m_screen->m_info.modesetDone) {
            // Restore the previous CRTC contents if any.
            const auto& saved = m_screen->m_info.savedCrtc;
            if (saved.crtc_id)
                drmIoctl(m_fd, DRM_IOCTL_MODE_SETCRTC,
                         const_cast<drm_mode_crtc*>(&saved));
            m_screen->m_info.modesetDone = false;
        }
    }

    void setTitle(const std::string&) override {}
    void setGeometry(const Rect& geometry) override {
        m_geometry = geometry;
    }
    void setVisible(bool visible) override { m_visible = visible; }

    void present(const std::uint32_t* pixels, const Size& size) override {
        if (!m_visible)
            return;
        DrmScreenInfo& info = m_screen->m_info;
        if (!info.map || size.width != info.mode.hdisplay ||
            size.height != info.mode.vdisplay)
            return; // only fullscreen surfaces for now
        const std::size_t bytes =
            static_cast<std::size_t>(size.width) * size.height * 4;
        std::memcpy(info.map, pixels, bytes < info.mapSize ? bytes : info.mapSize);
        if (!info.modesetDone) {
            drm_mode_crtc set{};
            set.crtc_id = info.crtcId;
            set.fb_id = info.fbId;
            set.set_connectors_ptr =
                reinterpret_cast<std::uint64_t>(&info.connectorId);
            set.count_connectors = 1;
            set.mode = info.mode;
            set.mode_valid = 1;
            if (drmIoctl(m_fd, DRM_IOCTL_MODE_SETCRTC, &set) == 0)
                info.modesetDone = true;
        }
    }

private:
    int m_fd;
    DrmScreen* m_screen;
    Rect m_geometry;
    bool m_visible = false;
};
class DrmDisplay : public DisplayBackend {
public:
    DrmDisplay() {
        const char* device = std::getenv("FIRMIR_DRM_DEVICE");
        m_fd = ::open(device && *device ? device : "/dev/dri/card0",
                      O_RDWR | O_CLOEXEC);
        if (m_fd < 0)
            return;
        refreshScreens();
        m_pollThread = std::thread([this] { pollHotplug(); });
    }

    ~DrmDisplay() override {
        m_stop = true;
        if (m_pollThread.joinable())
            m_pollThread.join();
        for (auto& screen : m_screens)
            destroyBuffer(screen->m_info);
        if (m_fd >= 0)
            ::close(m_fd);
    }

    std::vector<PlatformScreen*> screens() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<PlatformScreen*> result;
        for (const auto& screen : m_screens)
            result.push_back(screen.get());
        return result;
    }

    PlatformWindow* createWindow(PlatformScreen* screen) override {
        auto* drmScreen = static_cast<DrmScreen*>(screen);
        if (!drmScreen->m_info.map)
            createBuffer(drmScreen->m_info);
        auto* window = new DrmWindow(m_fd, drmScreen);
        m_windows.emplace_back(window);
        return window;
    }

    void destroyWindow(PlatformWindow* window) override {
        m_windows.erase(
            std::remove_if(m_windows.begin(), m_windows.end(),
                           [&](const auto& w) { return w.get() == window; }),
            m_windows.end());
    }

private:
    void createBuffer(DrmScreenInfo& info) {
        drm_mode_create_dumb create{};
        create.width = info.mode.hdisplay;
        create.height = info.mode.vdisplay;
        create.bpp = 32;
        if (drmIoctl(m_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0)
            return;
        info.handle = create.handle;
        info.pitch = create.pitch;
        info.mapSize = create.size;

        drm_mode_fb_cmd fb{};
        fb.width = create.width;
        fb.height = create.height;
        fb.bpp = 32;
        fb.depth = 24;
        fb.pitch = create.pitch;
        fb.handle = create.handle;
        if (drmIoctl(m_fd, DRM_IOCTL_MODE_ADDFB, &fb) != 0) {
            destroyBuffer(info);
            return;
        }
        info.fbId = fb.fb_id;

        drm_mode_map_dumb map{};
        map.handle = create.handle;
        if (drmIoctl(m_fd, DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) {
            destroyBuffer(info);
            return;
        }
        info.map = ::mmap(nullptr, create.size, PROT_READ | PROT_WRITE,
                          MAP_SHARED, m_fd, static_cast<off_t>(map.offset));
        if (info.map == MAP_FAILED) {
            info.map = nullptr;
            destroyBuffer(info);
        }
    }

    void destroyBuffer(DrmScreenInfo& info) {
        if (info.map) {
            ::munmap(info.map, info.mapSize);
            info.map = nullptr;
        }
        if (info.fbId) {
            std::uint32_t fb = info.fbId;
            drmIoctl(m_fd, DRM_IOCTL_MODE_RMFB, &fb);
            info.fbId = 0;
        }
        if (info.handle) {
            drm_mode_destroy_dumb destroy{};
            destroy.handle = info.handle;
            drmIoctl(m_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
            info.handle = 0;
        }
    }

    /// Enumerates connected connectors and pairs them with free CRTCs.
    std::vector<DrmScreenInfo> queryScreens() {
        std::vector<DrmScreenInfo> result;
        drm_mode_card_res res{};
        if (drmIoctl(m_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0)
            return result;
        std::vector<std::uint32_t> crtcs(res.count_crtcs);
        std::vector<std::uint32_t> encoders(res.count_encoders);
        std::vector<std::uint32_t> connectors(res.count_connectors);
        res.crtc_id_ptr = reinterpret_cast<std::uint64_t>(crtcs.data());
        res.encoder_id_ptr = reinterpret_cast<std::uint64_t>(encoders.data());
        res.connector_id_ptr =
            reinterpret_cast<std::uint64_t>(connectors.data());
        if (drmIoctl(m_fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0)
            return result;
        connectors.resize(res.count_connectors);
        std::vector<bool> crtcUsed(crtcs.size(), false);
        for (std::uint32_t connectorId : connectors) {
            DrmScreenInfo info;
            if (!queryConnector(connectorId, crtcs, crtcUsed, info))
                continue;
            result.push_back(std::move(info));
        }
        return result;
    }

    bool queryConnector(std::uint32_t connectorId,
                        const std::vector<std::uint32_t>& crtcs,
                        std::vector<bool>& crtcUsed, DrmScreenInfo& info) {
        drm_mode_get_connector conn{};
        conn.connector_id = connectorId;
        if (drmIoctl(m_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0 ||
            conn.connection != DRM_MODE_CONNECTED || conn.count_modes == 0)
            return false;
        std::vector<drm_mode_modeinfo> modes(conn.count_modes);
        std::vector<std::uint32_t> encoders(conn.count_encoders);
        conn.modes_ptr = reinterpret_cast<std::uint64_t>(modes.data());
        conn.encoders_ptr = reinterpret_cast<std::uint64_t>(encoders.data());
        if (drmIoctl(m_fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0)
            return false;

        info.connectorId = connectorId;
        info.mode = modes[0]; // first mode is the preferred one
        info.name = "connector-" + std::to_string(connectorId);

        // Find an encoder (prefer the current one) and a free CRTC it can
        // drive.
        for (std::uint32_t encoderId : encoders) {
            drm_mode_get_encoder enc{};
            enc.encoder_id = encoderId;
            if (drmIoctl(m_fd, DRM_IOCTL_MODE_GETENCODER, &enc) != 0)
                continue;
            for (std::size_t i = 0; i < crtcs.size(); ++i) {
                if (crtcUsed[i] || !(enc.possible_crtcs & (1u << i)))
                    continue;
                crtcUsed[i] = true;
                info.crtcId = crtcs[i];
                info.encoderId = encoderId;
                // Save the current CRTC state for restore on teardown.
                info.savedCrtc.crtc_id = info.crtcId;
                drmIoctl(m_fd, DRM_IOCTL_MODE_GETCRTC, &info.savedCrtc);
                return true;
            }
        }
        return false;
    }

    void refreshScreens() {
        auto current = queryScreens();
        std::lock_guard<std::mutex> lock(m_mutex);
        // Detect removed screens.
        for (auto it = m_screens.begin(); it != m_screens.end();) {
            const auto stillThere = std::any_of(
                current.begin(), current.end(), [&](const DrmScreenInfo& s) {
                    return s.connectorId == (*it)->m_info.connectorId;
                });
            if (!stillThere && screenCallback) {
                PlatformScreen* removed = it->get();
                it = m_screens.erase(it);
                screenCallback(removed, false);
            } else {
                ++it;
            }
        }
        // Detect added screens.
        for (auto& info : current) {
            const auto known = std::any_of(
                m_screens.begin(), m_screens.end(), [&](const auto& s) {
                    return s->m_info.connectorId == info.connectorId;
                });
            if (!known) {
                m_screens.push_back(std::make_unique<DrmScreen>(std::move(info)));
                if (screenCallback)
                    screenCallback(m_screens.back().get(), true);
            }
        }
    }

    void pollHotplug() {
        while (!m_stop) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (!m_stop)
                refreshScreens();
        }
    }

    int m_fd = -1;
    std::mutex m_mutex;
    std::vector<std::unique_ptr<DrmScreen>> m_screens;
    std::vector<std::unique_ptr<DrmWindow>> m_windows;
    std::thread m_pollThread;
    bool m_stop = false;
};

} // namespace

extern "C" DisplayBackend* firmir_gui_create_display() {
    return new DrmDisplay;
}

