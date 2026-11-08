#include "test_framework.hpp"

#include <firmir/gui-controls/controls.hpp>
#include <firmir/gui/fontdatabase.hpp>
#include <firmir/gui/flickable.hpp>
#include <firmir/gui/image.hpp>
#include <firmir/gui/mousearea.hpp>
#include <firmir/gui/shadereffect.hpp>
#include <firmir/gui/text.hpp>

using namespace Firmir;
using namespace Firmir::Gui;

TEST_CASE(gui_native_font_and_database) {
    Font font("DejaVu Sans", 24);
    font.setBold(true);
    font.setUnderline(true);
    Text text;
    text.setFont(font);
    text.setText("Привет, Firmir");
    CHECK(text.width() > 20);
    CHECK(text.height() >= 20);
    CHECK_EQ(text.fontPixelSize(), 24);
    CHECK(text.fontBold());
    CHECK(text.fontUnderline());
    text.setFontFamily("DejaVu Serif");
    CHECK_EQ(text.fontFamily().toStdString(), "DejaVu Serif");
    text.setProperty("font.italic", true);
    CHECK(text.fontItalic());
    const int id = FontDatabase::addApplicationFont(
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    CHECK(id >= 0);
    CHECK(!FontDatabase::applicationFontFamilies(id).empty());
    CHECK(FontDatabase::removeApplicationFont(id));
}

TEST_CASE(gui_rgba_image_and_shader_effect) {
    Image image;
    const std::uint8_t pixels[16] = {
        255, 0, 0, 255, 0, 255, 0, 255,
        0, 0, 255, 255, 255, 255, 255, 255
    };
    image.setRgba(2, 2, pixels, sizeof(pixels));
    CHECK(image.status() == Image::Status::Ready);
    CHECK_EQ(image.sourceSize().width, 2);
    CHECK_EQ(image.rgbaData().size(), sizeof(pixels));
    image.setFillMode(Image::FillMode::PreserveAspectCrop);
    CHECK(image.fillMode() == Image::FillMode::PreserveAspectCrop);
    ShaderEffect effect;
    effect.setSourceImage(&image);
    effect.setFragmentShader("gl_FragColor = texture2D(u_texture, v_uv);");
    CHECK(effect.sourceImage() == &image);
    CHECK(effect.fragmentShader().toStdString().find("texture2D") != std::string::npos);
}

TEST_CASE(gui_flickable_bounds_and_controls) {
    Flickable flickable;
    flickable.setGeometry(0, 0, 100, 100);
    flickable.setContentWidth(300);
    flickable.setContentHeight(400);
    flickable.setContentX(50);
    flickable.setContentY(1000);
    CHECK_EQ(flickable.contentX(), 50.0f);
    CHECK_EQ(flickable.contentY(), 300.0f);
    flickable.setContentX(-10);
    CHECK_EQ(flickable.contentX(), 0.0f);

    using namespace Controls;
    Item root;
    Button button(&root);
    int clicks = 0;
    button.connect<>(&button, AbstractButton::signalClicked, [&] { ++clicks; });
    button.click();
    CHECK_EQ(clicks, 1);
    CheckBox check(&root);
    check.setTristate(true);
    check.click();
    CHECK(check.checkState() == CheckBox::CheckState::PartiallyChecked);
    check.click();
    CHECK(check.checkState() == CheckBox::CheckState::Checked);
    RadioButton first(&root), second(&root);
    first.setChecked(true);
    second.click();
    CHECK(!first.isChecked());
    CHECK(second.isChecked());
    Switch toggle(&root);
    toggle.click();
    CHECK(toggle.isChecked());
}

TEST_CASE(gui_control_containers_and_indicators) {
    using namespace Controls;
    StackView stack;
    auto* first = new Item;
    auto* second = new Item;
    stack.push(first);
    stack.push(second);
    CHECK_EQ(stack.depth(), 2);
    CHECK(stack.currentItem() == second);
    CHECK(stack.pop() == second);
    CHECK(stack.currentItem() == first);
    delete first;
    delete second;

    SwipeView swipe;
    auto* page1 = new Item;
    auto* page2 = new Item;
    swipe.addItem(page1);
    swipe.addItem(page2);
    swipe.setCurrentIndex(1);
    CHECK_EQ(swipe.currentIndex(), 1);
    CHECK(swipe.currentItem() == page2);
    delete page1;
    delete page2;

    TabBar tabs;
    tabs.addTab("One");
    tabs.addTab("Two");
    tabs.setCurrentIndex(1);
    CHECK_EQ(tabs.currentIndex(), 1);
    CHECK(tabs.tabAt(1)->isChecked());

    ProgressBar progress;
    progress.setTo(200);
    progress.setValue(50);
    CHECK_EQ(progress.visualPosition(), 0.25f);
    progress.setIndeterminate(true);
    CHECK(progress.indeterminate());
    progress.setIndeterminate(false);
    BusyIndicator busy;
    busy.setRunning(false);
    CHECK(!busy.running());
}
