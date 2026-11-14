// Touch digitizer → display mapping for the evdev GUI backend
// (plugins/gui/evdev/touchmapping.hpp).
//
// The bug these guard against: raw evdev coordinates were passed on as window
// pixels, so a digitizer whose ranges are not the panel's pixel grid, or whose
// axes are rotated relative to the panel, put every tap somewhere else — on a
// portrait panel with a landscape digitizer, 90° away ("as if the touchscreen
// were rotated").

#include "test_framework.hpp"

#include "../../plugins/gui/evdev/touchmapping.hpp"

using namespace Firmir::Evdev::detail;

namespace {

TouchArea area(int xMin, int xMax, int yMin, int yMax, int screenWidth = 0,
               int screenHeight = 0) {
    TouchArea result;
    result.x = {xMin, xMax};
    result.y = {yMin, yMax};
    result.screenWidth = screenWidth;
    result.screenHeight = screenHeight;
    return result;
}

TouchTransform rotated(TouchRotation rotation, bool mirrorX = false,
                       bool mirrorY = false) {
    TouchTransform transform;
    transform.rotation = rotation;
    transform.mirrorX = mirrorX;
    transform.mirrorY = mirrorY;
    return transform;
}

} // namespace

TEST_CASE(gui_evdev_touch_normalizes_axis_range) {
    // Ranges do not start at zero: 100..1100 is a 1000-unit axis.
    CHECK_EQ(touchNormalize({100, 1100}, 100.0f), 0.0f);
    CHECK_EQ(touchNormalize({100, 1100}, 600.0f), 0.5f);
    CHECK_EQ(touchNormalize({100, 1100}, 1100.0f), 1.0f);
    // Values outside the range (firmware jitter) are clamped.
    CHECK_EQ(touchNormalize({100, 1100}, -50.0f), 0.0f);
    CHECK_EQ(touchNormalize({100, 1100}, 2000.0f), 1.0f);
    // A degenerate range never divides by zero.
    CHECK_EQ(touchNormalize({0, 0}, 42.0f), 0.0f);
}

TEST_CASE(gui_evdev_touch_maps_1to1_panel) {
    // Digitizer ranges equal to the panel pixels: the tap lands where pressed.
    const TouchArea touch = area(0, 1199, 0, 1999, 1200, 2000);
    const TouchPixel topLeft =
        mapTouchPosition(touch, rotated(TouchRotation::None), 0, 0);
    CHECK_EQ(topLeft.x, 0.0f);
    CHECK_EQ(topLeft.y, 0.0f);
    const TouchPixel middle =
        mapTouchPosition(touch, rotated(TouchRotation::None), 600, 1000);
    // Range 0..1199 covers 1200 pixels: the centre lands within half a pixel.
    CHECK(middle.x > 599.5f && middle.x < 601.5f);
    CHECK(middle.y > 999.5f && middle.y < 1001.5f);
    const TouchPixel bottomRight =
        mapTouchPosition(touch, rotated(TouchRotation::None), 1199, 1999);
    CHECK(bottomRight.x >= 1198.0f && bottomRight.x <= 1199.0f);
    CHECK(bottomRight.y >= 1998.0f && bottomRight.y <= 1999.0f);
}

TEST_CASE(gui_evdev_touch_scales_device_range_to_screen) {
    // A digitizer with a 0..4095 range on a 1200x2000 panel: without scaling,
    // every tap beyond the top-left corner would fall outside the window.
    const TouchArea touch = area(0, 4095, 0, 4095, 1200, 2000);
    const TouchPixel middle =
        mapTouchPosition(touch, rotated(TouchRotation::None), 2047.5f, 2047.5f);
    CHECK(middle.x > 599.0f && middle.x < 601.0f);
    CHECK(middle.y > 999.0f && middle.y < 1001.0f);
    const TouchPixel corner =
        mapTouchPosition(touch, rotated(TouchRotation::None), 4095.0f, 4095.0f);
    CHECK_EQ(corner.x, 1199.0f);
    CHECK_EQ(corner.y, 1999.0f);
}

TEST_CASE(gui_evdev_touch_rotates_clockwise) {
    // Landscape digitizer (0..1999 x 0..1199) on a portrait 1200x2000 panel.
    const TouchArea touch = area(0, 1999, 0, 1199, 1200, 2000);
    // Device top-left → screen top-right.
    const TouchPixel a =
        mapTouchPosition(touch, rotated(TouchRotation::Clockwise90), 0, 0);
    CHECK_EQ(a.x, 1199.0f);
    CHECK_EQ(a.y, 0.0f);
    // Device bottom-left → screen top-left.
    const TouchPixel b =
        mapTouchPosition(touch, rotated(TouchRotation::Clockwise90), 0, 1199);
    CHECK_EQ(b.x, 0.0f);
    CHECK_EQ(b.y, 0.0f);
    // Device bottom-right → screen bottom-left.
    const TouchPixel c = mapTouchPosition(touch, rotated(TouchRotation::Clockwise90),
                                          1999, 1199);
    CHECK_EQ(c.x, 0.0f);
    CHECK_EQ(c.y, 1999.0f);

    // 180° turns the panel upside down, 270° is the other 90° case.
    const TouchArea panel = area(0, 1199, 0, 1999, 1200, 2000);
    const TouchPixel upsideDown =
        mapTouchPosition(panel, rotated(TouchRotation::Clockwise180), 0, 0);
    CHECK_EQ(upsideDown.x, 1199.0f);
    CHECK_EQ(upsideDown.y, 1999.0f);
    const TouchPixel other =
        mapTouchPosition(touch, rotated(TouchRotation::Clockwise270), 0, 0);
    CHECK_EQ(other.x, 0.0f);
    CHECK_EQ(other.y, 1999.0f);
}

TEST_CASE(gui_evdev_touch_mirrors) {
    const TouchArea panel = area(0, 1199, 0, 1999, 1200, 2000);
    const TouchPixel mirrored =
        mapTouchPosition(panel, rotated(TouchRotation::None, true, false), 0, 0);
    CHECK_EQ(mirrored.x, 1199.0f);
    CHECK_EQ(mirrored.y, 0.0f);
    const TouchPixel both =
        mapTouchPosition(panel, rotated(TouchRotation::None, true, true), 0, 0);
    CHECK_EQ(both.x, 1199.0f);
    CHECK_EQ(both.y, 1999.0f);
}

TEST_CASE(gui_evdev_touch_auto_rotation) {
    // Digitizer and panel agree: no rotation.
    CHECK(touchAutoRotation(area(0, 1199, 0, 1999, 1200, 2000)) ==
          TouchRotation::None);
    CHECK(touchAutoRotation(area(0, 1999, 0, 1199, 1920, 1080)) ==
          TouchRotation::None);
    // Portrait digitizer on a landscape panel and vice versa: swap the axes.
    CHECK(touchAutoRotation(area(0, 1199, 0, 1999, 1920, 1080)) ==
          TouchRotation::Clockwise90);
    CHECK(touchAutoRotation(area(0, 1999, 0, 1199, 1200, 2000)) ==
          TouchRotation::Clockwise90);
    // Without a screen size there is nothing to compare against.
    CHECK(touchAutoRotation(area(0, 1999, 0, 1199)) == TouchRotation::None);
    CHECK(touchAutoRotation(area(0, 0, 0, 0, 1200, 2000)) == TouchRotation::None);
}

TEST_CASE(gui_evdev_touch_unknown_screen_keeps_device_pixels) {
    // No screen size (headless, or before the display appeared): the digitizer
    // extent is used, which is the old pass-through behaviour.
    const TouchArea touch = area(0, 1199, 0, 1999);
    const TouchPixel pixel =
        mapTouchPosition(touch, rotated(TouchRotation::None), 300, 2000);
    // The digitizer extent stands in for the screen: raw values stay pixels.
    CHECK(pixel.x > 299.0f && pixel.x < 301.0f);
    // Beyond the axis maximum: clamped, never negative or off-screen.
    CHECK_EQ(pixel.y, 1999.0f);
    const TouchPixel negative =
        mapTouchPosition(touch, rotated(TouchRotation::None), -5, -5);
    CHECK_EQ(negative.x, 0.0f);
    CHECK_EQ(negative.y, 0.0f);
}
