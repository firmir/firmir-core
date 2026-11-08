#pragma once

// Shared software rasterizer for rounded rectangles (SDF-based, ~1 px
// analytic antialiasing). Used by Rectangle (item) and Painter::fillRoundedRect
// (control chrome).

#include <firmir/gui/painter.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Firmir {
namespace Gui {
namespace detail {

/// Rasterizes a rounded rect with a border ring into straight-alpha RGBA8.
inline void rasterizeRoundedRect(int width, int height, float radius,
                                 float borderWidth, Color fill, Color border,
                                 std::vector<std::uint8_t>& out) {
    out.assign(static_cast<std::size_t>(width) * height * 4, 0);
    const float r = std::min(radius, std::min(width, height) / 2.0f);
    const float cx = width / 2.0f, cy = height / 2.0f;
    const float hx = cx - r, hy = cy - r;
    const float fillA = fill.a / 255.0f, borderA = border.a / 255.0f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float qx = std::fabs(px - cx) - hx;
            const float qy = std::fabs(py - cy) - hy;
            const float ax = std::max(qx, 0.0f), ay = std::max(qy, 0.0f);
            // Signed distance to the rounded box (< 0 inside).
            const float d = std::sqrt(ax * ax + ay * ay) +
                            std::min(std::max(qx, qy), 0.0f) - r;
            const float outer = std::clamp(0.5f - d, 0.0f, 1.0f);
            if (outer <= 0.0f)
                continue;
            float fillCov = outer, borderCov = 0.0f;
            if (borderWidth > 0.0f) {
                fillCov = std::clamp(0.5f - (d + borderWidth), 0.0f, 1.0f);
                borderCov = outer - fillCov;
            }
            // Fill over border, both scaled by their coverage.
            const float aF = fillA * fillCov;
            const float aB = borderA * borderCov * (1.0f - aF);
            const float alpha = aF + aB;
            if (alpha <= 0.0f)
                continue;
            auto channel = [&](std::uint8_t f, std::uint8_t b) {
                return static_cast<std::uint8_t>(
                    std::lround((f * aF + b * aB) / alpha));
            };
            std::uint8_t* p =
                out.data() + (static_cast<std::size_t>(y) * width + x) * 4;
            p[0] = channel(fill.r, border.r);
            p[1] = channel(fill.g, border.g);
            p[2] = channel(fill.b, border.b);
            p[3] = static_cast<std::uint8_t>(std::lround(alpha * 255.0f));
        }
    }
}

} // namespace detail
} // namespace Gui
} // namespace Firmir
