#include <firmir/gui/repeater.hpp>

namespace Firmir {
namespace Gui {

Repeater::Repeater(Object* parent) : Item(parent) {
    // The Item base constructor already emitted childrenChanged on the parent,
    // but at that point this object was still typed Item (the Repeater vptr was
    // not installed yet), so observers that discriminate on the dynamic type
    // (the positioners) could not recognize it. Re-emit now that construction
    // is complete.
    if (Object* p = this->parent())
        p->emitSignal("childrenChanged");
}

} // namespace Gui
} // namespace Firmir
