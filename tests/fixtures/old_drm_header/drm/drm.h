// Minimal stand-in for a pre-4.0 <drm/drm.h> as shipped by older targets:
// struct drm_buf_map declares a member literally named "virtual" (upstream only
// later wrapped it in #ifdef __cplusplus and renamed it to "virt"). Compiling
// against this header is what reproduces
//
//     error: expected unqualified-id before 'virtual'
//
// It only exists for the drmuapi.hpp regression test; it is never installed and
// shadows the system header for that one target only.
#ifndef FIRMIR_TEST_OLD_DRM_H
#define FIRMIR_TEST_OLD_DRM_H

#include <stdint.h>

#define DRM_IOCTL_BASE 'd'
#define DRM_COMMAND_BASE 0x40

struct drm_buf_pub {
    int idx;
    int total;
    int used;
    void* address;
};

struct drm_buf_map {
    int count;
    void* virtual; /* the offending member */
    struct drm_buf_pub* list;
};

struct drm_version {
    int version_major;
    int version_minor;
};

#include "drm_mode.h"

#endif
