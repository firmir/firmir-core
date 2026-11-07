#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/string.hpp>

namespace Firmir {
namespace Gui {

class Image;

/// Quad with a user fragment shader body.
/// The shader can use v_color, v_uv, u_texture, u_textured and gl_FragColor.
class ShaderEffect : public Item {
public:
    explicit ShaderEffect(Object* parent = nullptr);
    ~ShaderEffect() override;
    String fragmentShader() const { return m_fragmentShader; }
    void setFragmentShader(String shader);
    Color color() const { return m_color; }
    void setColor(Color color);
    Image* sourceImage() const { return m_source; }
    void setSourceImage(Image* source) { auto lock = lockScene(); m_source = source; update(); }
protected:
    void paint(Painter& painter) override;
private:
    String m_fragmentShader;
    Color m_color = Color::rgb(255, 255, 255);
    Image* m_source = nullptr;
};

} // namespace Gui
} // namespace Firmir
