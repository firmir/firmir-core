//
// C++ wrapper around the DRM/KMS uapi headers (<drm/drm.h>, <drm/drm_mode.h>).
//
// The kernel headers are plain C and not every copy out there compiles as C++:
// <drm/drm.h> from kernels older than ~4.0 declares a member called "virtual"
// in struct drm_buf_map (the DRM_IOCTL_MAP_BUFS argument), e.g. the header that
// some older distributions ship:
//
//     struct drm_buf_map {
//         int count;
//         void *virtual;      /* Mmap'd area in user-virtual */
//         struct drm_buf_pub *list;
//     };
//
// Upstream later wrapped that member in an #ifdef __cplusplus block (it is
// called "virt" in C++), so only old headers break the build with
// "expected unqualified-id before 'virtual'".
//
// firmir never touches drm_buf_map, so the keyword is renamed for the duration
// of the include only: the struct layout and every ioctl number stay exactly as
// the system header defines them (no ABI copy in firmir). Include this header
// instead of <drm/drm.h> and it works with both the old and the fixed headers.
//
//   #include <firmir/gui/detail/drmuapi.hpp>   // drm.h + drm_mode.h
//
#ifndef FIRMIR_GUI_DETAIL_DRMUAPI_HPP
#define FIRMIR_GUI_DETAIL_DRMUAPI_HPP

#include <cstddef>
#include <cstdint>

#if defined(__clang__)
// The macro deliberately hides a C++ keyword, which clang reports by default.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#endif

#ifdef __cplusplus
#define virtual firmir_drm_uapi_virtual_member
#endif
#include <drm/drm.h>
#ifdef __cplusplus
#undef virtual
#endif

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <drm/drm_mode.h>

#endif // FIRMIR_GUI_DETAIL_DRMUAPI_HPP
