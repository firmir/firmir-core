#pragma once

#include <firmir/gui/item.hpp>

namespace Firmir {
namespace Gui {

/**
 * @brief Delegate factory item: its children are the instantiated delegates.
 *
 * A Repeater itself is an inert Item; the delegate instantiation is driven by
 * the host (firmlc-generated code creates one delegate subtree per model row
 * as a direct child of the Repeater). Positioners (Column/Row/Grid) treat a
 * Repeater child as if its delegates were direct children of the positioner.
 */
class Repeater : public Item {
public:
    explicit Repeater(Object* parent = nullptr);
};

} // namespace Gui
} // namespace Firmir
