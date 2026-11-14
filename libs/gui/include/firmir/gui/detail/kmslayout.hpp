//
// Window→display geometry for the eglfs KMS integration.
//
// eglfs has no window manager, so a platform window *is* the output. A window
// that asks for a size other than the current mode therefore has two options,
// and this header holds both decisions so they can be tested without a GPU or a
// DRM device (tests/gui/test_kms_layout.cpp):
//
//   * kmsFindMode()  — a mode of exactly the requested size, if the connector
//                      has one (HDMI/DP monitors usually do; a fixed-resolution
//                      panel does not), so the window can be mode-set to it;
//   * kmsFrameRect() — where the frame goes inside the surface otherwise:
//                      centred and 1:1. Only a frame that does not fit at all is
//                      scaled, and then only *down* — a window is never blown up
//                      to fill the screen. The application adapts its content to
//                      the size it asked for, instead of being stretched.
//
//   #include <firmir/gui/detail/kmslayout.hpp>
//
#ifndef FIRMIR_GUI_DETAIL_KMSLAYOUT_HPP
#define FIRMIR_GUI_DETAIL_KMSLAYOUT_HPP

#include <firmir/gui/detail/drmuapi.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

// Ancient uapi headers may lack the interlace flag.
#ifndef DRM_MODE_FLAG_INTERLACE
#define DRM_MODE_FLAG_INTERLACE (1 << 4)
#endif

namespace Firmir::Gui::detail {

/// Integer rectangle in surface pixels.
struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/// The connector mode of exactly @p width x @p height, or nullptr.
///
/// Modes arrive sorted by preference (best refresh rate first), so the first
/// match wins. A progressive mode is preferred over an interlaced one of the
/// same size — the kernels report the interlaced variant first on some panels.
inline const drm_mode_modeinfo* kmsFindMode(
    const std::vector<drm_mode_modeinfo>& modes, int width, int height) {
    const drm_mode_modeinfo* match = nullptr;
    for (const drm_mode_modeinfo& mode : modes) {
        if (mode.hdisplay != width || mode.vdisplay != height)
            continue;
        if (!(mode.flags & DRM_MODE_FLAG_INTERLACE))
            return &mode;
        if (!match)
            match = &mode;
    }
    return match;
}

/// Where a @p width x @p height frame is drawn on a @p surfaceWidth x
/// @p surfaceHeight output: centred, 1:1, or scaled down to fit when the frame
/// is larger than the output. Returns an empty rectangle for empty input.
inline PixelRect kmsFrameRect(int surfaceWidth, int surfaceHeight, int width,
                              int height) {
    PixelRect rect;
    if (surfaceWidth <= 0 || surfaceHeight <= 0 || width <= 0 || height <= 0)
        return rect;
    int w = width;
    int h = height;
    if (w > surfaceWidth || h > surfaceHeight) {
        const double scale = std::min(
            static_cast<double>(surfaceWidth) / static_cast<double>(w),
            static_cast<double>(surfaceHeight) / static_cast<double>(h));
        w = std::max(1, static_cast<int>(w * scale));
        h = std::max(1, static_cast<int>(h * scale));
    }
    rect.width = w;
    rect.height = h;
    rect.x = (surfaceWidth - w) / 2;
    rect.y = (surfaceHeight - h) / 2;
    return rect;
}

} // namespace Firmir::Gui::detail

#endif // FIRMIR_GUI_DETAIL_KMSLAYOUT_HPP
