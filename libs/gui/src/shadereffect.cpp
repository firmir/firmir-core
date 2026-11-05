#include <firmir/gui/shadereffect.hpp>
#include <firmir/gui/image.hpp>
#include "detail/color_convert.hpp"

namespace Firmir {
namespace Gui {

ShaderEffect::ShaderEffect(Object* parent) : Item(parent),
    m_fragmentShader("gl_FragColor = vec4(v_uv.x, v_uv.y, 0.5, 1.0) * v_color;") {
    setImplicitWidth(128);
    setImplicitHeight(128);
    registerProperty<String>("fragmentShader", &ShaderEffect::fragmentShader,
                             &ShaderEffect::setFragmentShader);
    registerPropertyImpl("color", typeid(Color),
        [](const Object* o) -> std::any { return static_cast<const ShaderEffect*>(o)->color(); },
        [](Object* o, const std::any& v) { static_cast<ShaderEffect*>(o)->setColor(detail::anyToColor(v)); });
}
ShaderEffect::~ShaderEffect() = default;
void ShaderEffect::setFragmentShader(String shader) {
    auto sceneLock = lockScene();
    if (m_fragmentShader == shader) return;
    m_fragmentShader = std::move(shader);
    update();
    notifyPropertyChanged("fragmentShader");
}
void ShaderEffect::setColor(Color color) {
    auto sceneLock = lockScene();
    if (m_color == color) return;
    m_color = color;
    update();
    notifyPropertyChanged("color");
}
void ShaderEffect::paint(Painter& painter) {
    const void* pixels = nullptr;
    int width = 0, height = 0;
    std::uint64_t generation = 0;
    if (m_source && m_source->status() == Image::Status::Ready) {
        pixels = m_source->rgbaData().data();
        width = m_source->sourceSize().width;
        height = m_source->sourceSize().height;
        generation = m_source->rgbaGeneration();
    }
    painter.drawShader(RectF{0, 0, this->width(), this->height()},
                       m_fragmentShader.toStdString(), this, m_color,
                       pixels, width, height, generation);
}

} // namespace Gui
} // namespace Firmir
