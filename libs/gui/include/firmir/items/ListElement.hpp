#pragma once

// Firml-facing item header (capitalized item name). ListElement has no
// runtime class: firmlc compiles ListElement declarations into ListModel
// append() calls. This header exists so that `#include
// <firmir/items/ListElement.hpp>` resolves and declares the type name.

namespace Firmir {
namespace Gui {
class ListElement;
} // namespace Gui
} // namespace Firmir
