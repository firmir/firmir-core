#pragma once

// Firmir::Gui::Component — a deferred item factory, the runtime backing of
// the Firml `Component { ... }` element:
//
//   Component {
//       id: delegateComponent
//       Rectangle { ... }
//   }
//   // in a handler: delegateComponent.createObject(someParent)
//
// The component holds a factory function; createObject() instantiates the
// declared subtree under the given parent. Each call builds a fresh subtree
// (the ids inside the component are per-instance).

#include <firmir/object.hpp>

#include <functional>
#include <memory>

namespace Firmir {
namespace Gui {

class Item;

class Component : public Object {
public:
    /// Builds the subtree under @p parent and returns its root item.
    using Factory = std::function<Item*(Item* parent)>;

    explicit Component(Factory factory, Object* parent = nullptr)
        : Object(parent), d(std::make_unique<Impl>(Impl{std::move(factory)})) {}

    /// Instantiates the component under @p parent (may be nullptr for a
    /// detached tree). The created tree is owned through the parent.
    Item* createObject(Item* parent) {
        return d->factory ? d->factory(parent) : nullptr;
    }

private:
    struct Impl {
        Factory factory;
    };
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
