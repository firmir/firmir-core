// The two-call DRM ioctl protocol behind the eglfs KMS integration
// (firmir/gui/detail/drmenumeration.hpp).
//
// The regression this guards against: DRM_IOCTL_MODE_GETRESOURCES reports
// count_fbs and DRM_IOCTL_MODE_GETCONNECTOR reports count_props, and the kernel
// rejects the "fill" call with -EFAULT unless every non-zero count comes with a
// non-NULL array. The integration used to send the counts back without the fbs
// and property arrays, so enumeration failed on every device that owned a
// framebuffer or had connector properties — i.e. on all real hardware, where it
// showed up as "device integration kms has no outputs". The fake kernel below
// answers with EFAULT in exactly that case and counts it, so the test fails if
// the protocol is ever under-supplied again.

#include "test_framework.hpp"

#include <firmir/gui/detail/drmenumeration.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace Firmir::Gui::detail;

namespace {

struct FakeDevice {
    struct FakeConnector {
        std::uint32_t id = 0;
        std::uint32_t type = 0;
        std::uint32_t status = DRM_MODE_CONNECTED;
        std::uint32_t encoderId = 0;
        std::uint32_t props = 3; // every connector has CRTC_ID, DPMS, EDID
        std::vector<drm_mode_modeinfo> modes;
        std::vector<std::uint32_t> encoders;
    };

    std::vector<std::uint32_t> fbs{70};
    std::vector<std::uint32_t> crtcs{40, 41};
    std::vector<std::uint32_t> encoders{60, 61};
    std::vector<FakeConnector> connectors;
    std::uint32_t possibleCrtcs = 0x3;
    bool failResources = false;
    int rejectedNullPointers = 0; // must stay 0
};

FakeDevice* g_device = nullptr;

int eagain() {
    ++g_device->rejectedNullPointers;
    errno = EFAULT;
    return -1;
}

template <typename T>
int copyOut(std::uint64_t ptr, const std::vector<T>& values,
            std::uint32_t requested) {
    if (requested == 0)
        return 0;
    const auto count = values.size() < requested ? values.size() : requested;
    if (count)
        std::memcpy(reinterpret_cast<void*>(static_cast<std::uintptr_t>(ptr)),
                    values.data(), count * sizeof(T));
    return 0;
}

drm_mode_modeinfo mode(std::uint16_t width, std::uint16_t height,
                       const char* name) {
    drm_mode_modeinfo info{};
    info.hdisplay = width;
    info.vdisplay = height;
    info.vrefresh = 60;
    std::strncpy(info.name, name, sizeof(info.name) - 1);
    return info;
}

FakeDevice::FakeConnector* connector(std::uint32_t id) {
    for (auto& c : g_device->connectors)
        if (c.id == id)
            return &c;
    return nullptr;
}

int fakeIoctl(int, unsigned long request, void* arg) {
    FakeDevice& dev = *g_device;
    if (request == DRM_IOCTL_MODE_GETRESOURCES) {
        auto* res = static_cast<drm_mode_card_res*>(arg);
        if (dev.failResources) {
            errno = ENODEV;
            return -1;
        }
        std::vector<std::uint32_t> connectorIds;
        for (const auto& c : dev.connectors)
            connectorIds.push_back(c.id);
        // The kernel fails the whole call if a reported count has no buffer.
        if ((res->count_fbs && !res->fb_id_ptr) ||
            (res->count_crtcs && !res->crtc_id_ptr) ||
            (res->count_encoders && !res->encoder_id_ptr) ||
            (res->count_connectors && !res->connector_id_ptr))
            return eagain();
        copyOut(res->fb_id_ptr, dev.fbs, res->count_fbs);
        copyOut(res->crtc_id_ptr, dev.crtcs, res->count_crtcs);
        copyOut(res->encoder_id_ptr, dev.encoders, res->count_encoders);
        copyOut(res->connector_id_ptr, connectorIds, res->count_connectors);
        res->count_fbs = dev.fbs.size();
        res->count_crtcs = dev.crtcs.size();
        res->count_encoders = dev.encoders.size();
        res->count_connectors = connectorIds.size();
        return 0;
    }

    if (request == DRM_IOCTL_MODE_GETCONNECTOR) {
        auto* conn = static_cast<drm_mode_get_connector*>(arg);
        FakeDevice::FakeConnector* found = connector(conn->connector_id);
        if (!found) {
            errno = ENOENT;
            return -1;
        }
        // Same rule as above: a non-zero count needs its array, count_props
        // and prop_values_ptr included.
        if ((conn->count_modes && !conn->modes_ptr) ||
            (conn->count_encoders && !conn->encoders_ptr) ||
            (conn->count_props && (!conn->props_ptr || !conn->prop_values_ptr)))
            return eagain();
        // The property arrays are written through raw pointers: the integration
        // never looks at them, it only has to provide the buffers.
        copyOut(conn->modes_ptr, found->modes, conn->count_modes);
        copyOut(conn->encoders_ptr, found->encoders, conn->count_encoders);
        for (std::uint32_t i = 0; i < found->props && i < conn->count_props;
             ++i) {
            auto* ids = reinterpret_cast<std::uint32_t*>(
                static_cast<std::uintptr_t>(conn->props_ptr));
            auto* values = reinterpret_cast<std::uint64_t*>(
                static_cast<std::uintptr_t>(conn->prop_values_ptr));
            ids[i] = 100 + i;
            values[i] = 0;
        }
        conn->connection = found->status;
        conn->connector_type = found->type;
        conn->encoder_id = found->encoderId;
        conn->count_modes = found->modes.size();
        conn->count_encoders = found->encoders.size();
        conn->count_props = found->props;
        return 0;
    }

    if (request == DRM_IOCTL_MODE_GETENCODER) {
        auto* enc = static_cast<drm_mode_get_encoder*>(arg);
        enc->possible_crtcs = dev.possibleCrtcs;
        return 0;
    }

    if (request == DRM_IOCTL_MODE_GETCRTC) {
        auto* crtc = static_cast<drm_mode_crtc*>(arg);
        crtc->fb_id = dev.fbs.front();
        crtc->mode_valid = 1;
        crtc->mode = mode(1024, 768, "previous");
        return 0;
    }

    errno = ENOTTY;
    return -1;
}

} // namespace

TEST_CASE(gui_drm_enumeration_protocol) {
    FakeDevice device;
    FakeDevice::FakeConnector hdmi;
    hdmi.id = 31;
    hdmi.type = 11; // DRM_MODE_CONNECTOR_HDMIA
    hdmi.status = DRM_MODE_CONNECTED;
    hdmi.encoderId = 60;
    hdmi.modes = {mode(1920, 1080, "1920x1080"), mode(1280, 720, "1280x720")};
    hdmi.encoders = {60};
    FakeDevice::FakeConnector vga;
    vga.id = 32;
    vga.type = 1; // DRM_MODE_CONNECTOR_VGA
    vga.status = DRM_MODE_DISCONNECTED;
    vga.encoders = {61};
    device.connectors = {hdmi, vga};
    g_device = &device;

    const DrmEnumerationResult result = enumerateDrmOutputs(3, fakeIoctl);
    g_device = nullptr;

    // Nothing was requested without its buffer: the old code tripped over
    // count_fbs in GETRESOURCES and count_props in GETCONNECTOR.
    CHECK_EQ(device.rejectedNullPointers, 0);
    CHECK(result.error.empty());
    CHECK_EQ(result.connectors.size(), std::size_t(2));
    CHECK_EQ(result.outputs.size(), std::size_t(1));
    const DrmOutput& output = result.outputs.front();
    CHECK_EQ(output.connectorId, std::uint32_t(31));
    CHECK_EQ(output.crtcId, std::uint32_t(40));
    CHECK_EQ(output.encoderId, std::uint32_t(60));
    CHECK_EQ(output.mode.hdisplay, std::uint16_t(1920));
    CHECK_EQ(output.mode.vdisplay, std::uint16_t(1080));
    CHECK_EQ(output.name, std::string("connector-31"));
    // The previous CRTC state is kept so the window can restore it.
    CHECK_EQ(output.savedCrtc.crtc_id, std::uint32_t(40));
    CHECK_EQ(output.savedCrtc.mode_valid, 1u);
    CHECK_EQ(output.savedCrtc.mode.hdisplay, std::uint16_t(1024));

    CHECK(result.connectors[0].usable);
    CHECK_EQ(std::string(drmConnectorStatusName(result.connectors[0].status)),
             std::string("connected"));
    CHECK_EQ(result.connectors[0].modeCount, std::uint32_t(2));
    CHECK(!result.connectors[1].usable);
    CHECK_EQ(result.connectors[1].note, std::string("disconnected"));
}

TEST_CASE(gui_drm_enumeration_skips_unusable_connectors) {
    FakeDevice device;
    FakeDevice::FakeConnector panel;
    panel.id = 33;
    panel.status = DRM_MODE_CONNECTED;
    panel.encoders = {60}; // connected, but the driver reports no modes yet
    device.connectors = {panel};
    g_device = &device;

    const DrmEnumerationResult result = enumerateDrmOutputs(3, fakeIoctl);
    g_device = nullptr;

    CHECK_EQ(device.rejectedNullPointers, 0);
    CHECK(result.error.empty());
    CHECK(result.outputs.empty());
    CHECK_EQ(result.connectors.size(), std::size_t(1));
    CHECK_EQ(result.connectors[0].note, std::string("no modes"));
    CHECK(!result.connectors[0].usable);
}

TEST_CASE(gui_drm_enumeration_needs_a_free_crtc) {
    FakeDevice device;
    device.crtcs = {40};
    FakeDevice::FakeConnector first;
    first.id = 31;
    first.encoders = {60};
    first.modes = {mode(1920, 1080, "1920x1080")};
    FakeDevice::FakeConnector second = first;
    second.id = 34;
    device.connectors = {first, second};
    g_device = &device;

    const DrmEnumerationResult result = enumerateDrmOutputs(3, fakeIoctl);
    g_device = nullptr;

    CHECK_EQ(result.outputs.size(), std::size_t(1));
    CHECK_EQ(result.outputs.front().connectorId, std::uint32_t(31));
    CHECK_EQ(result.connectors.size(), std::size_t(2));
    CHECK_EQ(result.connectors[1].note, std::string("no free CRTC"));
}

TEST_CASE(gui_drm_enumeration_reports_unusable_device) {
    FakeDevice device;
    device.failResources = true;
    g_device = &device;

    const DrmEnumerationResult result = enumerateDrmOutputs(3, fakeIoctl);
    g_device = nullptr;

    CHECK(result.outputs.empty());
    CHECK(result.connectors.empty());
    // The plugin logs this string, so it has to name the failing ioctl.
    CHECK(result.error.find("DRM_IOCTL_MODE_GETRESOURCES") != std::string::npos);
    CHECK(result.error.find("ENODEV") != std::string::npos ||
          result.error.find("No such device") != std::string::npos);
}
