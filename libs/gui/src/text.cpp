#include <firmir/gui/text.hpp>
#include "detail/color_convert.hpp"
#include "detail/fontengine.hpp"

#include <algorithm>
#include <utility>

namespace Firmir {
namespace Gui {

class Text::Impl {
public:
    int scale() const { return std::max(1, (fontSize + 7) / 8); }
    void updateContent(Text* q) {
        if (customFont) {
            bitmap = detail::rasterizeText(text.toStdString(), font);
            ++generation;
        } else {
            bitmap = {};
        }
        if (customFont && bitmap.valid) {
            q->setImplicitWidth(bitmap.width);
            q->setImplicitHeight(bitmap.height);
        } else {
            float w = 0, h = 0;
            Painter::textSize(text.toStdString(), scale(), w, h);
            q->setImplicitWidth(w);
            q->setImplicitHeight(h);
        }
    }
    String text;
    Color color;
    // Native rendering with the default font family (Inter) is the default;
    // the built-in 8x8 bitmap font remains as a fallback when the font stack
    // (Fontconfig/FreeType/HarfBuzz) is unavailable.
    Font font{kDefaultFontFamily, 16};
    detail::RasterizedText bitmap;
    std::uint64_t generation = 0;
    int fontSize = 16;
    bool customFont = true;
};

Text::Text(Object* parent) : Item(parent), d(std::make_unique<Impl>()) {
    registerProperty<String>("text", &Text::text, &Text::setText);
    registerPropertyImpl("color", typeid(Color),
        [](const Object* o) -> std::any { return static_cast<const Text*>(o)->color(); },
        [](Object* o, const std::any& v) { static_cast<Text*>(o)->setColor(detail::anyToColor(v)); });
    registerProperty<Font>("font", &Text::font, &Text::setFont);
    registerProperty<int>("fontSize", &Text::fontSize, &Text::setFontSize);
    registerProperty<String>("font.family", &Text::fontFamily, &Text::setFontFamily);
    registerProperty<int>("font.pixelSize", &Text::fontPixelSize, &Text::setFontPixelSize);
    registerProperty<float>("font.pointSize", &Text::fontPointSize, &Text::setFontPointSize);
    registerProperty<bool>("font.bold", &Text::fontBold, &Text::setFontBold);
    registerProperty<bool>("font.italic", &Text::fontItalic, &Text::setFontItalic);
    registerProperty<bool>("font.underline", &Text::fontUnderline, &Text::setFontUnderline);
    registerProperty<bool>("font.strikeout", &Text::fontStrikeOut, &Text::setFontStrikeOut);
}
Text::~Text() = default;
String Text::text() const { return d->text; }
void Text::setText(String text) {
    auto sceneLock = lockScene();
    if (d->text == text) return;
    d->text = std::move(text);
    d->updateContent(this);
    update();
    notifyPropertyChanged("text");
}
Color Text::color() const { return d->color; }
void Text::setColor(Color color) {
    auto sceneLock = lockScene();
    if (d->color == color) return;
    d->color = color;
    update();
    notifyPropertyChanged("color");
}
Font Text::font() const { return d->font; }
void Text::setFont(Font font) {
    auto sceneLock = lockScene();
    d->customFont = true;
    if (d->font == font) { d->updateContent(this); update(); return; }
    d->font = std::move(font);
    d->fontSize = d->font.effectivePixelSize();
    d->updateContent(this);
    update();
    notifyPropertyChanged("font");
}
int Text::fontSize() const { return d->fontSize; }
void Text::setFontSize(int size) {
    auto sceneLock = lockScene();
    if (size <= 0 || d->fontSize == size) return;
    d->fontSize = size;
    if (d->customFont) d->font.setPixelSize(size);
    d->updateContent(this);
    update();
    notifyPropertyChanged("fontSize");
    if (d->customFont) notifyPropertyChanged("font.pixelSize");
}
String Text::fontFamily() const { return String(d->font.family()); }
void Text::setFontFamily(String family) {
    auto sceneLock = lockScene();
    Font value = d->font; value.setFamily(family.toStdString()); setFont(value);
    notifyPropertyChanged("font.family");
}
int Text::fontPixelSize() const { return d->font.effectivePixelSize(); }
void Text::setFontPixelSize(int pixels) {
    auto sceneLock = lockScene();
    Font value = d->font; value.setPixelSize(pixels); setFont(value);
    d->fontSize = d->font.effectivePixelSize();
    notifyPropertyChanged("font.pixelSize");
}
float Text::fontPointSize() const {
    return d->font.pointSize() > 0 ? d->font.pointSize() : d->font.effectivePixelSize() * 72.0f / 96.0f;
}
void Text::setFontPointSize(float points) {
    auto sceneLock = lockScene();
    Font value = d->font; value.setPointSize(points); setFont(value);
    d->fontSize = d->font.effectivePixelSize();
    notifyPropertyChanged("font.pointSize");
}
bool Text::fontBold() const { return d->font.bold(); }
void Text::setFontBold(bool bold) {
    auto sceneLock = lockScene();
    Font value = d->font; value.setBold(bold); setFont(value); notifyPropertyChanged("font.bold");
}
bool Text::fontItalic() const { return d->font.italic(); }
void Text::setFontItalic(bool italic) {
    auto sceneLock = lockScene();
    Font value = d->font; value.setItalic(italic); setFont(value); notifyPropertyChanged("font.italic");
}
bool Text::fontUnderline() const { return d->font.underline(); }
void Text::setFontUnderline(bool underline) {
    auto sceneLock = lockScene();
    Font value = d->font; value.setUnderline(underline); setFont(value);
    notifyPropertyChanged("font.underline");
}
bool Text::fontStrikeOut() const { return d->font.strikeOut(); }
void Text::setFontStrikeOut(bool strikeOut) {
    auto sceneLock = lockScene();
    Font value = d->font; value.setStrikeOut(strikeOut); setFont(value);
    notifyPropertyChanged("font.strikeout");
}
void Text::paint(Painter& painter) {
    if (d->text.isEmpty()) return;
    if (d->customFont && d->bitmap.valid) {
        painter.drawRgba(RectF{0, 0, static_cast<float>(d->bitmap.width),
                              static_cast<float>(d->bitmap.height)},
                         d->bitmap.rgba.data(), d->bitmap.width, d->bitmap.height,
                         this, d->generation, d->color);
    } else {
        painter.drawText(0, 0, d->text.toStdString(), d->color, d->scale());
    }
}
} // namespace Gui
} // namespace Firmir
