// E2E for Component: createObject() instantiates the deferred subtree, the
// returned object is writable, and the per-instance handlers work.
#include "component.h"

#include <firmir/gui/button.hpp>
#include <firmir/gui/component.hpp>
#include <firmir/gui/rectangle.hpp>
#include <firmir/gui/text.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/window.hpp>
#include <headless_control.hpp>

#include <any>
#include <cstdio>
#include <cstdlib>

#define CHECK(x)                                                                  \
    do {                                                                          \
        if (!(x)) {                                                               \
            std::fprintf(stderr, "component e2e: CHECK failed at line %d\n",      \
                         __LINE__);                                               \
            return 1;                                                             \
        }                                                                         \
    } while (false)

namespace {

double numberProp(Firmir::Object* obj, const char* name) {
    return std::any_cast<double>(obj->getProperty(name));
}

} // namespace

int main() {
    ::setenv("FIRMIR_GUI_DISPLAY", "headless", 1);
    ::setenv("FIRMIR_GUI_INPUT", "headless", 1);
    auto* platform = Firmir::Gui::Platform::instance();
    firmir_gui_headless_add_screen("COMPONENT-1", 640, 480);
    Firmir::Gui::Window window(platform->screens().front());
    window.resize(300, 200);
    ComponentObjects objects = createComponent(window.contentItem());
    window.show();

    // Nothing is instantiated before createObject().
    CHECK(objects.badgeComponent != nullptr);
    CHECK(objects.container->children().empty());
    CHECK(numberProp(objects.root, "spawned") == 0.0);

    // First instantiation through the generated handler (spawnButton click).
    objects.spawnButton->emitSignal("clicked");
    CHECK(numberProp(objects.root, "spawned") == 1.0);
    CHECK(objects.container->children().size() == 1);
    auto* badge1 = objects.container->findChild<Firmir::Gui::Rectangle>("badge");
    CHECK(badge1 != nullptr);
    CHECK(badge1->parent() == objects.container);
    CHECK(badge1->width() == 50.0);
    // The handler wrote the returned object's custom property.
    CHECK(std::any_cast<Firmir::String>(badge1->getProperty("label")) ==
          Firmir::String("spawned"));
    // Per-instance ids resolve inside the instance: the Text is centered.
    auto* text1 = badge1->findChild<Firmir::Gui::Text>("badgeText");
    CHECK(text1 != nullptr);
    CHECK(text1->x() == (50 - text1->width()) / 2);

    // The instance's own handler works: clicking its Button sets the label.
    auto* button1 = badge1->findChild<Firmir::Gui::Button>("badgeButton");
    CHECK(button1 != nullptr);
    button1->emitSignal("clicked");
    CHECK(std::any_cast<Firmir::String>(badge1->getProperty("label")) ==
          Firmir::String("clicked"));

    // Second instantiation: a fresh independent subtree.
    objects.spawnButton->emitSignal("clicked");
    CHECK(numberProp(objects.root, "spawned") == 2.0);
    CHECK(objects.container->children().size() == 2);
    // The first instance kept its own state.
    CHECK(std::any_cast<Firmir::String>(badge1->getProperty("label")) ==
          Firmir::String("clicked"));

    // Direct C++ use of the component: createObject() without the handler.
    Firmir::Gui::Item* badge3 =
        objects.badgeComponent->createObject(objects.container);
    CHECK(badge3 != nullptr);
    CHECK(badge3->parent() == objects.container);
    CHECK(objects.container->children().size() == 3);

    firmir_gui_headless_remove_screen("COMPONENT-1");
    std::puts("component e2e: OK");
    return 0;
}
