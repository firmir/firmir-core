#pragma once

// controls-gallery application logic, exposed to the Firml UI
// (gallery.fml): page switching through the "page" property (-1 = menu),
// the StackView demo through pushStackPage()/popStackPage(), the Image /
// ShaderEffect / TabBar demos wired from main() through attach().

#include <firmir/object.hpp>
#include <firmir/string.hpp>

namespace Firmir {
namespace Gui {
class Image;
class ShaderEffect;
namespace Controls {
class StackView;
class TabBar;
} // namespace Controls
} // namespace Gui
} // namespace Firmir

class GalleryApp : public Firmir::Object {
public:
    explicit GalleryApp(Object* parent = nullptr);

    /// Wires the FML-created demo objects that need C++ data (called once
    /// from main()).
    void attach(Firmir::Gui::Controls::StackView* stack,
                Firmir::Gui::Image* image,
                Firmir::Gui::Image* imageFit,
                Firmir::Gui::ShaderEffect* texturedEffect,
                Firmir::Gui::Image* shaderSource,
                Firmir::Gui::Controls::TabBar* tabs);

    // ---- called from the Firml handlers ----
    void showPage(double page);
    void showMenu();
    void pushStackPage();
    void popStackPage();

private:
    Firmir::Gui::Controls::StackView* m_stack = nullptr; // not owned
};
