//
// Touch digitizer → display mapping for the evdev GUI backend.
//
// An evdev touchscreen reports raw device coordinates: ABS_X/ABS_Y (or
// ABS_MT_POSITION_X/Y) between the axis minimum and maximum, in the digitizer's
// own orientation. The display has its own size and orientation, and the two do
// not have to agree — a panel mounted in portrait with a digitizer that reports
// landscape coordinates puts every tap 90° away from where the user pressed.
//
// This header turns one raw report into a pixel position on the screen:
//
//   * the raw value is scaled from [min, max] to [0, 1] per axis;
//   * rotation (0/90/180/270 clockwise, optional mirroring) fixes an orientation
//     mismatch;
//   * [0, 1] is scaled to the screen size.
//
// Rotation comes from FIRMIR_EVDEV_TOUCH_ROTATION / FIRMIR_EVDEV_TOUCH_FLIP
// when they name one; "auto" (the default) swaps the axes when the digitizer's
// extent has the opposite aspect ratio of the screen — the usual "the
// touchscreen is rotated by 90°" case.
//
// Dependency-free on purpose, so the mapping is testable without a device
// (tests/gui/test_evdev_touch.cpp).
//
//   #include "touchmapping.hpp"
//
#ifndef FIRMIR_EVDEV_TOUCHMAPPING_HPP
#define FIRMIR_EVDEV_TOUCHMAPPING_HPP

#include <algorithm>

namespace Firmir::Evdev::detail {

/// One absolute axis of the digitizer, as reported by EVIOCGABS.
struct TouchAxis {
    int minimum = 0;
    int maximum = 0;
};

/// The digitizer's axes plus the size of the display they belong to.
struct TouchArea {
    TouchAxis x;
    TouchAxis y;
    /// Screen size in pixels; 0 when unknown (the device extent is used then).
    int screenWidth = 0;
    int screenHeight = 0;
};

enum class TouchRotation { None = 0, Clockwise90 = 90, Clockwise180 = 180, Clockwise270 = 270 };

/// How a raw report is rotated/mirrored onto the display.
struct TouchTransform {
    TouchRotation rotation = TouchRotation::None;
    bool mirrorX = false;
    bool mirrorY = false;
};

/// Pixel position on the screen.
struct TouchPixel {
    float x = 0.0f;
    float y = 0.0f;
};

/// The rotation that puts the digitizer's long axis on the screen's long axis.
///
/// A portrait digitizer on a landscape screen (and the other way round) is the
/// 90°/270° case; which of the two directions is right cannot be told from the
/// ranges alone, so Clockwise90 is used and the caller may mirror/override.
inline TouchRotation touchAutoRotation(const TouchArea& area) {
    const int deviceWidth = area.x.maximum - area.x.minimum;
    const int deviceHeight = area.y.maximum - area.y.minimum;
    if (deviceWidth <= 0 || deviceHeight <= 0 || area.screenWidth <= 0 ||
        area.screenHeight <= 0)
        return TouchRotation::None;
    const bool deviceLandscape = deviceWidth >= deviceHeight;
    const bool screenLandscape = area.screenWidth >= area.screenHeight;
    return deviceLandscape != screenLandscape ? TouchRotation::Clockwise90
                                              : TouchRotation::None;
}

/// A raw axis value mapped to 0..1.
inline float touchNormalize(const TouchAxis& axis, float value) {
    const int span = axis.maximum - axis.minimum;
    if (span <= 0)
        return 0.0f;
    const float normalized = (value - axis.minimum) / static_cast<float>(span);
    return std::clamp(normalized, 0.0f, 1.0f);
}

/// Maps one raw report to a position on the screen (clamped to it).
inline TouchPixel mapTouchPosition(const TouchArea& area,
                                   const TouchTransform& transform, float rawX,
                                   float rawY) {
    float u = touchNormalize(area.x, rawX);
    float v = touchNormalize(area.y, rawY);
    switch (transform.rotation) {
    case TouchRotation::Clockwise90: {
        const float rotated = 1.0f - v;
        v = u;
        u = rotated;
        break;
    }
    case TouchRotation::Clockwise180:
        u = 1.0f - u;
        v = 1.0f - v;
        break;
    case TouchRotation::Clockwise270: {
        const float rotated = v;
        v = 1.0f - u;
        u = rotated;
        break;
    }
    case TouchRotation::None:
        break;
    }
    if (transform.mirrorX)
        u = 1.0f - u;
    if (transform.mirrorY)
        v = 1.0f - v;

    // Screen size when known, the digitizer extent otherwise (which is the same
    // 1:1 mapping a panel whose ranges are its pixels needs).
    const float width = area.screenWidth > 0
                            ? static_cast<float>(area.screenWidth)
                            : static_cast<float>(area.x.maximum - area.x.minimum + 1);
    const float height = area.screenHeight > 0
                             ? static_cast<float>(area.screenHeight)
                             : static_cast<float>(area.y.maximum - area.y.minimum + 1);
    TouchPixel pixel;
    pixel.x = std::clamp(u * width, 0.0f, width - 1.0f);
    pixel.y = std::clamp(v * height, 0.0f, height - 1.0f);
    return pixel;
}

} // namespace Firmir::Evdev::detail

#endif // FIRMIR_EVDEV_TOUCHMAPPING_HPP
