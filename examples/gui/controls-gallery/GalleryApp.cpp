#include "GalleryApp.hpp"

#include <firmir/gui-controls/containers.hpp>
#include <firmir/gui/image.hpp>
#include <firmir/gui/item.hpp>
#include <firmir/gui/rectangle.hpp>
#include <firmir/gui/shadereffect.hpp>
#include <firmir/gui/text.hpp>

#include <cstdint>
#include <vector>

using namespace Firmir;
using namespace Firmir::Gui;
namespace C = Firmir::Gui::Controls;

GalleryApp::GalleryApp(Object* parent) : Object(parent) {
    setProperty("page", -1.0); // -1 = the menu page
    setProperty("depthText", String("Depth: 1"));
}

void GalleryApp::attach(C::StackView* stack, Image* image, Image* imageFit,
                        ShaderEffect* texturedEffect, Image* shaderSource,
                        C::TabBar* tabs) {
    m_stack = stack;

    // The Image demo: a procedural 16x16 pattern (no asset files shipped).
    std::vector<std::uint8_t> pixels(16 * 16 * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = 220;
        pixels[i + 1] = 120;
        pixels[i + 2] = 40;
        pixels[i + 3] = 255;
    }
    image->setRgba(16, 16, pixels.data(), pixels.size());
    imageFit->setRgba(16, 16, pixels.data(), pixels.size());
    shaderSource->setRgba(16, 16, pixels.data(), pixels.size());
    texturedEffect->setSourceImage(shaderSource);

    tabs->addTab("General");
    tabs->addTab("Details");
    tabs->addTab("About");
}

void GalleryApp::showPage(double page) { setProperty("page", page); }
void GalleryApp::showMenu() { setProperty("page", -1.0); }

void GalleryApp::pushStackPage() {
    if (!m_stack)
        return;
    const int depth = m_stack->depth() + 1;
    static const Color palette[] = {
        Color::rgb(40, 85, 105), Color::rgb(105, 65, 45),
        Color::rgb(45, 105, 75), Color::rgb(85, 55, 110)};
    auto* item = new Item(m_stack);
    auto* rect = new Rectangle(item);
    rect->fill(item);
    rect->setColor(palette[(depth - 1) % 4]);
    auto* label = new Text(item);
    label->setX(20);
    label->setY(20);
    label->setFontSize(20);
    label->setColor(Color::rgb(255, 255, 255));
    label->setText(String(("Page " + std::to_string(depth)).c_str()));
    m_stack->push(item);
    setProperty("depthText",
                String(("Depth: " + std::to_string(m_stack->depth())).c_str()));
}

void GalleryApp::popStackPage() {
    if (!m_stack || m_stack->depth() <= 1)
        return; // keep the initial page
    m_stack->pop()->deleteLater();
    setProperty("depthText",
                String(("Depth: " + std::to_string(m_stack->depth())).c_str()));
}
