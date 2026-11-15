#pragma once

// Horizontal slices of a rounded rectangle, for callers that have a picture and
// no shader: a clip region can only be rectangular, so a rounded picture is
// drawn as a straight middle plus a staircase of slices where the corner arcs
// are (the same staircase a software rasterizer produces). Used by
// Painter::drawRounded*().

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace Firmir {
namespace Gui {
namespace detail {

/// One horizontal slice of a rounded rectangle.
struct RoundedBand {
    /// Top edge and height, as fractions of the rectangle's height.
    float top = 0.0f;
    float height = 1.0f;
    /// How far the slice is inset from both sides, as a fraction of the width.
    float inset = 0.0f;
};

/// Calls @p emit for every slice of a rectangle with corners of @p radius
/// pixels, top to bottom. Drawing a picture into all of them leaves the area
/// outside the corner arcs empty.
inline void forEachRoundedBand(
    float width, float height, float radius,
    const std::function<void(const RoundedBand&)>& emit) {
    if (width <= 0.0f || height <= 0.0f || !emit)
        return;
    const float r = std::min(radius, std::min(width, height) / 2.0f);
    if (r < 1.0f) {
        emit(RoundedBand{0.0f, 1.0f, 0.0f});
        return;
    }
    // How far the picture is inset on a row @p row rows below the top edge,
    // measured to the row's middle: the arc of a circle of radius r centred
    // (r, r) from the corner.
    const auto insetAt = [r](int row) {
        const float dy = std::max(0.0f, r - (static_cast<float>(row) + 0.5f));
        return r - std::sqrt(std::max(0.0f, r * r - dy * dy));
    };
    const int rows = std::min(static_cast<int>(height / 2.0f),
                              static_cast<int>(std::ceil(r)));
    // Rows the arc cuts into; below them the picture spans the full width.
    int cornerRows = 0;
    while (cornerRows < rows && insetAt(cornerRows) >= 1.0f)
        ++cornerRows;
    if (cornerRows == 0) {
        emit(RoundedBand{0.0f, 1.0f, 0.0f});
        return;
    }
    // Consecutive rows with the same whole-pixel inset are one band: the
    // staircase only has as many steps as the arc has pixels, and the caller
    // then issues that many draws instead of one per row.
    struct Step {
        int firstRow = 0;
        int rowCount = 0;
        int inset = 0;
    };
    std::vector<Step> corner;
    for (int row = 0; row < cornerRows; ++row) {
        const int inset = static_cast<int>(std::floor(insetAt(row)));
        if (!corner.empty() && corner.back().inset == inset) {
            ++corner.back().rowCount;
            continue;
        }
        corner.push_back(Step{row, 1, inset});
    }
    const auto band = [&](const Step& step, int firstRow) {
        emit(RoundedBand{static_cast<float>(firstRow) / height,
                         static_cast<float>(step.rowCount) / height,
                         static_cast<float>(step.inset) / width});
    };
    for (const Step& step : corner)
        band(step, step.firstRow);
    const float straight = static_cast<float>(height - 2 * cornerRows);
    if (straight > 0.0f) {
        emit(RoundedBand{static_cast<float>(cornerRows) / height,
                         straight / height, 0.0f});
    }
    for (auto it = corner.rbegin(); it != corner.rend(); ++it)
        band(*it, height - it->firstRow - it->rowCount);
}

} // namespace detail
} // namespace Gui
} // namespace Firmir
