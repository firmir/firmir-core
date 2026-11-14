// Window→display geometry for the eglfs KMS integration
// (firmir/gui/detail/kmslayout.hpp).
//
// The regression these guard against: a window whose size was not exactly the
// output mode was stretched over the whole screen, so an application could not
// choose its own size on bare KMS. Now a window gets a mode of its own when the
// connector has one, and is otherwise drawn 1:1 in the middle of the output —
// never blown up to fill it.

#include "test_framework.hpp"

#include <firmir/gui/detail/kmslayout.hpp>

#include <cstdint>
#include <vector>

using namespace Firmir::Gui::detail;

namespace {

drm_mode_modeinfo mode(int width, int height, std::uint32_t flags = 0) {
    drm_mode_modeinfo result{};
    result.hdisplay = static_cast<std::uint16_t>(width);
    result.vdisplay = static_cast<std::uint16_t>(height);
    result.flags = flags;
    return result;
}

} // namespace

TEST_CASE(gui_kms_find_mode_exact_size) {
    // A monitor exposes its modes preferred first, so the first exact match is
    // the best refresh rate for that size.
    const std::vector<drm_mode_modeinfo> modes = {mode(1200, 2000), mode(340, 506),
                                                  mode(340, 506, 0x10)};
    const drm_mode_modeinfo* found = kmsFindMode(modes, 340, 506);
    CHECK(found != nullptr);
    CHECK_EQ(found->hdisplay, std::uint16_t(340));
    CHECK_EQ(found->vdisplay, std::uint16_t(506));
    CHECK_EQ(found->flags, std::uint32_t(0));
}

TEST_CASE(gui_kms_find_mode_prefers_progressive) {
    // Some panels report the interlaced variant of a size first.
    const std::vector<drm_mode_modeinfo> modes = {mode(1920, 1080, 0x10),
                                                  mode(1920, 1080)};
    const drm_mode_modeinfo* found = kmsFindMode(modes, 1920, 1080);
    CHECK(found != nullptr);
    CHECK_EQ(found->flags, std::uint32_t(0));
}

TEST_CASE(gui_kms_find_mode_without_match) {
    // A fixed-mode panel: the window size has no mode, the output keeps its own.
    const std::vector<drm_mode_modeinfo> modes = {mode(1200, 2000)};
    CHECK(kmsFindMode(modes, 340, 506) == nullptr);
    CHECK(kmsFindMode(std::vector<drm_mode_modeinfo>{}, 340, 506) == nullptr);
    // Same width, other height is not a match either.
    CHECK(kmsFindMode(modes, 1200, 1920) == nullptr);
}

TEST_CASE(gui_kms_frame_rect_centre_1to1) {
    // The window keeps its size: it is centred, not scaled up to the output.
    const PixelRect rect = kmsFrameRect(1200, 2000, 340, 506);
    CHECK_EQ(rect.width, 340);
    CHECK_EQ(rect.height, 506);
    CHECK_EQ(rect.x, (1200 - 340) / 2);
    CHECK_EQ(rect.y, (2000 - 506) / 2);

    // A full-size window fills the output exactly.
    const PixelRect full = kmsFrameRect(1200, 2000, 1200, 2000);
    CHECK_EQ(full.x, 0);
    CHECK_EQ(full.y, 0);
    CHECK_EQ(full.width, 1200);
    CHECK_EQ(full.height, 2000);
}

TEST_CASE(gui_kms_frame_rect_scales_only_down) {
    // 2400x4000 does not fit a 1200x2000 output: it is scaled down to fit,
    // keeping the aspect ratio, and never up beyond the output.
    const PixelRect rect = kmsFrameRect(1200, 2000, 2400, 4000);
    CHECK_EQ(rect.width, 1200);
    CHECK_EQ(rect.height, 2000);
    CHECK_EQ(rect.x, 0);
    CHECK_EQ(rect.y, 0);

    // A very wide frame keeps its aspect ratio and leaves bars top and bottom.
    const PixelRect wide = kmsFrameRect(1200, 2000, 2400, 1000);
    CHECK_EQ(wide.width, 1200);
    CHECK_EQ(wide.height, 500);
    CHECK_EQ(wide.x, 0);
    CHECK_EQ(wide.y, (2000 - 500) / 2);

    // Larger than the output in height only.
    const PixelRect tall = kmsFrameRect(1200, 2000, 600, 4000);
    CHECK_EQ(tall.width, 300);
    CHECK_EQ(tall.height, 2000);
}

TEST_CASE(gui_kms_frame_rect_degenerate) {
    // Nothing to draw: present() drops such frames, the rectangle stays empty.
    const PixelRect empty = kmsFrameRect(0, 2000, 340, 506);
    CHECK_EQ(empty.width, 0);
    CHECK_EQ(empty.height, 0);
    CHECK_EQ(empty.x, 0);
    CHECK_EQ(empty.y, 0);
    CHECK_EQ(kmsFrameRect(1200, 2000, 0, 506).width, 0);
    CHECK_EQ(kmsFrameRect(1200, 2000, 340, -1).height, 0);
}
