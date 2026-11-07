#pragma once

#include <firmir/gui/font.hpp>

#include <string>

namespace Firmir {
namespace Gui {
namespace detail {

// The public rasterization API lives in <firmir/gui/font.hpp>; these aliases
// keep the internal detail:: spelling used across the GUI sources.
using RasterizedText = Gui::RasterizedText;
using Gui::rasterizeText;

} // namespace detail
} // namespace Gui
} // namespace Firmir
