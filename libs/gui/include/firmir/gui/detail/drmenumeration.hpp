//
// KMS mode-object enumeration for the eglfs KMS device integration.
//
// The DRM ioctl ABI is a "count first, then fill" protocol: the first call to
// DRM_IOCTL_MODE_GETRESOURCES / DRM_IOCTL_MODE_GETCONNECTOR only reports how
// many objects exist, the second call copies them into user arrays. The kernel
// does not look at the counts on their own: every non-zero count must come with
// a matching non-NULL array, otherwise the copy_to_user() for that field fails
// and the whole ioctl returns -EFAULT. Two traps follow from that, and both of
// them silently turn into "the device has no outputs":
//
//  * DRM_IOCTL_MODE_GETRESOURCES reports count_fbs too. Re-using its reply as
//    the request of the second call (so count_fbs is non-zero) without an fbs
//    array fails with -EFAULT on every device that owns a framebuffer, i.e. on
//    every device whose CRTC has ever been set up.
//  * DRM_IOCTL_MODE_GETCONNECTOR reports count_props. Passing modes_ptr and
//    encoders_ptr but no props_ptr/prop_values_ptr fails the same way; every
//    connector has properties (CRTC_ID, DPMS, EDID, ...).
//
// libdrm solves this by allocating all of the arrays, including the ones it
// never reads, and this header does the same. It also retries when the object
// counts change between the two calls (hotplug), as libdrm does.
//
// Header-only, with the ioctl function injectable so the protocol can be tested
// against a fake kernel (tests/gui/test_drm_enumeration.cpp).
//
//   #include <firmir/gui/detail/drmenumeration.hpp>
//
#ifndef FIRMIR_GUI_DETAIL_DRMENUMERATION_HPP
#define FIRMIR_GUI_DETAIL_DRMENUMERATION_HPP

#include <firmir/gui/detail/drmuapi.hpp>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>
#include <sys/ioctl.h>
#include <vector>

// Older kernel headers lack enum drm_connector_status.
#ifndef DRM_MODE_CONNECTED
#define DRM_MODE_CONNECTED 1
#define DRM_MODE_DISCONNECTED 2
#define DRM_MODE_UNKNOWNCONNECTION 3
#endif

namespace Firmir::Gui::detail {

/// The ioctl() signature, so tests can supply a fake kernel.
using DrmIoctl = int (*)(int fd, unsigned long request, void* arg);

/// ::ioctl() with the usual EINTR/EAGAIN retry loop.
inline int drmIoctl(int fd, unsigned long request, void* arg) {
    int ret;
    do {
        ret = ::ioctl(fd, request, arg);
    } while (ret == -1 && (errno == EINTR || errno == EAGAIN));
    return ret;
}

/// A connector that is (or may be) connected, with the mode and the CRTC
/// chosen for it.
struct DrmOutput {
    std::uint32_t connectorId = 0;
    std::uint32_t crtcId = 0;
    std::uint32_t encoderId = 0;
    drm_mode_modeinfo mode{};
    std::string name;
    /// Pre-eglfs CRTC state, restored when the window goes away.
    drm_mode_crtc savedCrtc{};
};

/// What happened to one connector, for diagnostics.
struct DrmConnectorReport {
    std::uint32_t connectorId = 0;
    std::uint32_t type = 0;
    std::uint32_t status = 0;
    std::uint32_t modeCount = 0;
    std::uint32_t encoderCount = 0;
    bool usable = false;
    std::string note; ///< why it did not become an output
};

struct DrmEnumerationResult {
    std::vector<DrmOutput> outputs;
    std::vector<DrmConnectorReport> connectors;
    std::string error; ///< empty unless the device could not be queried
};

inline const char* drmConnectorStatusName(std::uint32_t status) {
    switch (status) {
    case DRM_MODE_CONNECTED:
        return "connected";
    case DRM_MODE_DISCONNECTED:
        return "disconnected";
    case DRM_MODE_UNKNOWNCONNECTION:
        return "unknown";
    default:
        return "invalid";
    }
}

namespace drmdetail {

/// Address of the array for the *_ptr members of the DRM request structs;
/// 0 (NULL) for an empty array, which is what a zero count expects.
template <typename T>
inline std::uint64_t ptrOf(const std::vector<T>& values) {
    return static_cast<std::uint64_t>(
        reinterpret_cast<std::uintptr_t>(values.data()));
}

/// The kernel only copies min(requested, actual) entries, so an array that is
/// still too small after the counts were re-read means the object list changed
/// between the two calls.
inline bool buffersTooSmall(const std::vector<drm_mode_modeinfo>& modes,
                            std::uint32_t countModes,
                            const std::vector<std::uint32_t>& encoders,
                            std::uint32_t countEncoders,
                            const std::vector<std::uint32_t>& props,
                            std::uint32_t countProps) {
    return countModes > modes.size() || countEncoders > encoders.size() ||
        countProps > props.size();
}

} // namespace drmdetail

/// Enumerates the connectors of @p fd that have modes and picks a free CRTC for
/// each of them. Never throws; a device that cannot be queried is reported
/// through DrmEnumerationResult::error, a connector that cannot be used through
/// DrmConnectorReport::note.
inline DrmEnumerationResult enumerateDrmOutputs(int fd,
                                               DrmIoctl ioctlFn = drmIoctl) {
    DrmEnumerationResult result;
    if (!ioctlFn)
        ioctlFn = drmIoctl;

    // Resources: ask for the counts, then for the objects behind them. Repeat
    // if objects appeared or disappeared in between.
    std::vector<std::uint32_t> fbs;
    std::vector<std::uint32_t> crtcs;
    std::vector<std::uint32_t> encoders;
    std::vector<std::uint32_t> connectorIds;
    bool haveResources = false;
    for (int attempt = 0; attempt < 3 && !haveResources; ++attempt) {
        drm_mode_card_res counts{};
        if (ioctlFn(fd, DRM_IOCTL_MODE_GETRESOURCES, &counts) != 0) {
            result.error = std::string("DRM_IOCTL_MODE_GETRESOURCES: ") +
                std::strerror(errno);
            return result;
        }
        fbs.assign(counts.count_fbs, 0);
        crtcs.assign(counts.count_crtcs, 0);
        encoders.assign(counts.count_encoders, 0);
        connectorIds.assign(counts.count_connectors, 0);

        // A fresh request: every count that is sent needs its array, count_fbs
        // included (the framebuffers are never read, but they are reported).
        drm_mode_card_res request{};
        request.count_fbs = counts.count_fbs;
        request.count_crtcs = counts.count_crtcs;
        request.count_connectors = counts.count_connectors;
        request.count_encoders = counts.count_encoders;
        request.fb_id_ptr = drmdetail::ptrOf(fbs);
        request.crtc_id_ptr = drmdetail::ptrOf(crtcs);
        request.connector_id_ptr = drmdetail::ptrOf(connectorIds);
        request.encoder_id_ptr = drmdetail::ptrOf(encoders);
        if (ioctlFn(fd, DRM_IOCTL_MODE_GETRESOURCES, &request) != 0) {
            result.error = std::string("DRM_IOCTL_MODE_GETRESOURCES: ") +
                std::strerror(errno);
            return result;
        }
        if (request.count_fbs > fbs.size() ||
            request.count_crtcs > crtcs.size() ||
            request.count_encoders > encoders.size() ||
            request.count_connectors > connectorIds.size())
            continue; // hotplug between the two calls: ask again
        crtcs.resize(request.count_crtcs);
        encoders.resize(request.count_encoders);
        connectorIds.resize(request.count_connectors);
        haveResources = true;
    }
    if (!haveResources) {
        result.error = "DRM resources keep changing";
        return result;
    }

    std::vector<bool> crtcUsed(crtcs.size(), false);
    for (const std::uint32_t connectorId : connectorIds) {
        DrmConnectorReport report;
        report.connectorId = connectorId;

        drm_mode_get_connector probe{};
        probe.connector_id = connectorId;
        if (ioctlFn(fd, DRM_IOCTL_MODE_GETCONNECTOR, &probe) != 0) {
            report.note = std::string("DRM_IOCTL_MODE_GETCONNECTOR: ") +
                std::strerror(errno);
            result.connectors.push_back(std::move(report));
            continue;
        }

        std::vector<drm_mode_modeinfo> modes;
        std::vector<std::uint32_t> connectorEncoders;
        drm_mode_get_connector conn = probe;
        bool filled = false;
        for (int attempt = 0; attempt < 3 && !filled; ++attempt) {
            modes.assign(conn.count_modes, drm_mode_modeinfo{});
            connectorEncoders.assign(conn.count_encoders, 0);
            // Not read by the integration, but the kernel refuses to copy the
            // connector properties into a missing buffer (-EFAULT).
            std::vector<std::uint32_t> props(conn.count_props, 0);
            std::vector<std::uint64_t> propValues(conn.count_props, 0);

            drm_mode_get_connector request = probe;
            request.count_modes = conn.count_modes;
            request.count_encoders = conn.count_encoders;
            request.count_props = conn.count_props;
            request.modes_ptr = drmdetail::ptrOf(modes);
            request.encoders_ptr = drmdetail::ptrOf(connectorEncoders);
            request.props_ptr = drmdetail::ptrOf(props);
            request.prop_values_ptr = drmdetail::ptrOf(propValues);
            if (ioctlFn(fd, DRM_IOCTL_MODE_GETCONNECTOR, &request) != 0) {
                report.note = std::string("DRM_IOCTL_MODE_GETCONNECTOR: ") +
                    std::strerror(errno);
                break;
            }
            // Only the counts and the status are kept; the buffer pointers in
            // request point at temporaries of this iteration.
            conn.count_modes = request.count_modes;
            conn.count_encoders = request.count_encoders;
            conn.count_props = request.count_props;
            conn.connection = request.connection;
            conn.encoder_id = request.encoder_id;
            if (drmdetail::buffersTooSmall(modes, request.count_modes,
                                           connectorEncoders,
                                           request.count_encoders, props,
                                           request.count_props))
                continue; // modes/properties appeared in between: ask again
            modes.resize(request.count_modes);
            connectorEncoders.resize(request.count_encoders);
            filled = true;
        }

        report.status = conn.connection;
        report.type = conn.connector_type;
        report.modeCount = static_cast<std::uint32_t>(modes.size());
        report.encoderCount =
            static_cast<std::uint32_t>(connectorEncoders.size());
        if (!filled) {
            if (report.note.empty())
                report.note = "connector kept changing";
            result.connectors.push_back(std::move(report));
            continue;
        }
        if (conn.connection == DRM_MODE_DISCONNECTED) {
            report.note = "disconnected";
            result.connectors.push_back(std::move(report));
            continue;
        }
        if (modes.empty()) {
            report.note = "no modes";
            result.connectors.push_back(std::move(report));
            continue;
        }
        if (connectorEncoders.empty()) {
            report.note = "no encoder";
            result.connectors.push_back(std::move(report));
            continue;
        }

        // The encoder the connector is already attached to goes first, the rest
        // of its possible encoders follow.
        std::vector<std::uint32_t> candidates;
        if (conn.encoder_id)
            candidates.push_back(conn.encoder_id);
        for (const std::uint32_t encoderId : connectorEncoders)
            if (encoderId != conn.encoder_id)
                candidates.push_back(encoderId);

        std::uint32_t chosenCrtc = 0;
        std::uint32_t chosenEncoder = 0;
        for (const std::uint32_t encoderId : candidates) {
            drm_mode_get_encoder enc{};
            enc.encoder_id = encoderId;
            if (ioctlFn(fd, DRM_IOCTL_MODE_GETENCODER, &enc) != 0)
                continue;
            for (std::size_t i = 0; i < crtcs.size() && i < 32; ++i) {
                if (crtcUsed[i] || !(enc.possible_crtcs & (1u << i)))
                    continue;
                chosenCrtc = crtcs[i];
                chosenEncoder = encoderId;
                crtcUsed[i] = true;
                break;
            }
            if (chosenCrtc)
                break;
        }
        if (!chosenCrtc) {
            report.note = crtcs.empty() ? "no CRTC" : "no free CRTC";
            result.connectors.push_back(std::move(report));
            continue;
        }

        DrmOutput output;
        output.connectorId = connectorId;
        output.crtcId = chosenCrtc;
        output.encoderId = chosenEncoder;
        output.mode = modes.front(); // the preferred mode comes first
        output.name = "connector-" + std::to_string(connectorId);
        output.savedCrtc.crtc_id = chosenCrtc;
        ioctlFn(fd, DRM_IOCTL_MODE_GETCRTC, &output.savedCrtc);

        report.usable = true;
        result.connectors.push_back(std::move(report));
        result.outputs.push_back(std::move(output));
    }
    return result;
}

} // namespace Firmir::Gui::detail

#endif // FIRMIR_GUI_DETAIL_DRMENUMERATION_HPP
