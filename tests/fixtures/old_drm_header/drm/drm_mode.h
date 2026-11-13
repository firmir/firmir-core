// Minimal stand-in for the parts of <drm/drm_mode.h> the GUI backends use.
// Only present so the drmuapi.hpp regression test can compile without the
// system DRM headers; structures are trimmed to what is needed for the layout
// checks.
#ifndef FIRMIR_TEST_OLD_DRM_MODE_H
#define FIRMIR_TEST_OLD_DRM_MODE_H

#include <stdint.h>

#define DRM_MODE_CONNECTED 1
#define DRM_MODE_DISCONNECTED 2
#define DRM_MODE_UNKNOWNCONNECTION 3

struct drm_mode_modeinfo {
    uint32_t clock;
    uint16_t hdisplay;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t hskew;
    uint16_t vdisplay;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    uint16_t vscan;
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
    char name[32];
};

struct drm_mode_crtc {
    uint64_t set_connectors_ptr;
    uint32_t count_connectors;
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t x;
    uint32_t y;
    uint32_t gamma_size;
    uint32_t mode_valid;
    struct drm_mode_modeinfo mode;
};

#endif
