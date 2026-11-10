#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/string.hpp>

#include <memory>

namespace Firmir {
namespace Gui {

class Image;

/// Quad with a user fragment shader body.
/// The shader can use v_color, v_uv, u_texture, u_textured and gl_FragColor.
class ShaderEffect : public Item {
public:
    explicit ShaderEffect(Object* parent = nullptr);
    ~ShaderEffect() override;
    String fragmentShader() const;
    void setFragmentShader(String shader);
    Color color() const;
    void setColor(Color color);
    Image* sourceImage() const;
    void setSourceImage(Image* source);
protected:
    void paint(Painter& painter) override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
