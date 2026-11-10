#include <firmir/gui/shadereffect.hpp>
#include <firmir/gui/image.hpp>
#include "detail/color_convert.hpp"

namespace Firmir {
namespace Gui {

class ShaderEffect::Impl {
public:
    String fragmentShader{"gl_FragColor = vec4(v_uv.x, v_uv.y, 0.5, 1.0) * v_color;"};
    Color color = Color::rgb(255, 255, 255);
    Image* source = nullptr;
};

ShaderEffect::ShaderEffect(Object* parent)
    : Item(parent), d(std::make_unique<Impl>()) {
    setImplicitWidth(128);
    setImplicitHeight(128);
    registerProperty<String>("fragmentShader", &ShaderEffect::fragmentShader,
                             &ShaderEffect::setFragmentShader);
    registerPropertyImpl("color", typeid(Color),
        [](const Object* o) -> std::any { return static_cast<const ShaderEffect*>(o)->color(); },
        [](Object* o, const std::any& v) { static_cast<ShaderEffect*>(o)->setColor(detail::anyToColor(v)); });
}
ShaderEffect::~ShaderEffect() = default;
String ShaderEffect::fragmentShader() const { return d->fragmentShader; }
void ShaderEffect::setFragmentShader(String shader) {
    auto sceneLock = lockScene();
    if (d->fragmentShader == shader) return;
    d->fragmentShader = std::move(shader);
    update();
    notifyPropertyChanged("fragmentShader");
}
Color ShaderEffect::color() const { return d->color; }
void ShaderEffect::setColor(Color color) {
    auto sceneLock = lockScene();
    if (d->color == color) return;
    d->color = color;
    update();
    notifyPropertyChanged("color");
}
Image* ShaderEffect::sourceImage() const { return d->source; }
void ShaderEffect::setSourceImage(Image* source) {
    auto sceneLock = lockScene();
    d->source = source;
    update();
}
void ShaderEffect::paint(Painter& painter) {
    const void* pixels = nullptr;
    int width = 0, height = 0;
    std::uint64_t generation = 0;
    if (d->source && d->source->status() == Image::Status::Ready) {
        pixels = d->source->rgbaData().data();
        width = d->source->sourceSize().width;
        height = d->source->sourceSize().height;
        generation = d->source->rgbaGeneration();
    }
    painter.drawShader(RectF{0, 0, this->width(), this->height()},
                       d->fragmentShader.toStdString(), this, d->color,
                       pixels, width, height, generation);
}

} // namespace Gui
} // namespace Firmir
