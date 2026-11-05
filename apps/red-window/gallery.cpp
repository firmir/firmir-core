#include <firmir/gui-controls/controls.hpp>
#include <firmir/gui/fontdatabase.hpp>
#include <firmir/gui/image.hpp>
#include <firmir/gui/mousearea.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/rectangle.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/shadereffect.hpp>
#include <firmir/gui/text.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>
#ifdef FIRMIR_DEMO_HEADLESS
#include <headless_control.hpp>
#endif

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

using namespace Firmir;
using namespace Firmir::Gui;
namespace C = Firmir::Gui::Controls;

namespace {

Text* label(Item* parent, const std::string& text, float x, float y, int size = 16,
            Color color = Color::rgb(230, 235, 242), bool bold = false) {
    auto* item = new Text(parent);
    item->setX(x);
    item->setY(y);
    item->setColor(color);
    Font font("DejaVu Sans", size);
    font.setBold(bold);
    item->setFont(font);
    item->setText(String(text));
    return item;
}

Rectangle* rectangle(Item* parent, RectF geometry, Color color) {
    auto* item = new Rectangle(parent);
    item->setGeometry(geometry.x, geometry.y, geometry.width, geometry.height);
    item->setColor(color);
    return item;
}

C::Button* button(Item* parent, const std::string& text, RectF geometry,
                  std::function<void()> action = {}) {
    auto* item = new C::Button(parent);
    item->setText(String(text));
    item->setGeometry(geometry.x, geometry.y, geometry.width, geometry.height);
    if (action)
        item->connect<>(item, C::AbstractButton::signalClicked,
                        [action = std::move(action)] { action(); }, ConnectionMode::Direct);
    return item;
}

class Gallery {
public:
    Gallery(Window& window, int width, int height)
        : m_window(window), m_width(width), m_height(height) {
        auto* root = window.contentItem();
        rectangle(root, RectF{0, 0, static_cast<float>(width), static_cast<float>(height)},
                  Color::rgb(20, 25, 33))->fill(root);
        label(root, "Firmir Controls Gallery", 12, 10, 20, Color::rgb(245, 248, 252), true);
        m_stack = new C::StackView(root);
        m_stack->setGeometry(8, 46, width - 16, height - 54);
        createWindowButton("_", width - 132, [this] { m_window.showMinimized(); });
        createWindowButton("[]", width - 86, [this] {
            m_maximized = !m_maximized;
            if (m_maximized) m_window.showMaximized(); else m_window.showNormal();
        });
        createWindowButton("X", width - 40, [this] { m_window.close(); });
        showMenu();
    }

    void createWindowButton(const std::string& text, float x, std::function<void()> action) {
        button(m_window.contentItem(), text, RectF{x, 6, 36, 32}, std::move(action));
    }

    void showMenu() {
        auto* scroll = new C::ScrollView(m_stack);
        const int columns = std::max(1, (m_width - 32) / 175);
        static const char* names[] = {
            "Button", "Check / Radio / Switch", "ScrollView", "SplitView",
            "StackView", "SwipeView", "TabBar", "Busy / Progress",
            "Text & Fonts", "Image", "ShaderEffect", "MouseArea", "Flickable"
        };
        constexpr int count = static_cast<int>(sizeof(names) / sizeof(names[0]));
        const int rows = (count + columns - 1) / columns;
        Item* content = scroll->contentItem();
        for (int i = 0; i < count; ++i) {
            const int row = i / columns, column = i % columns;
            button(content, names[i], RectF{8.0f + column * 175.0f,
                   8.0f + row * 46.0f, 165, 36}, [this, i] { showPage(i); });
        }
        scroll->setContentSize(columns * 175.0f, rows * 46.0f + 12);
        m_stack->push(scroll);
    }

    Item* newPage(const std::string& title) {
        auto* page = new Item(m_stack);
        page->setVisible(false);
        button(page, "< Menu", RectF{8, 8, 96, 32}, [this, page] {
            m_stack->pop();
            page->deleteLater();
        });
        label(page, title, 116, 13, 18, Color::rgb(245, 248, 252), true);
        return page;
    }

    void showPage(int index);
private:
    Window& m_window;
    int m_width, m_height;
    C::StackView* m_stack;
    bool m_maximized = false;
};

void Gallery::showPage(int index) {
    const float contentWidth = m_width - 36;
    const float contentHeight = m_height - 112;
    Item* page = nullptr;
    switch (index) {
    case 0: {
        page = newPage("Button");
        auto* counter = label(page, "Clicked: 0", 20, 65, 18);
        int clicks = 0;
        button(page, "Push me", RectF{20, 105, 170, 42},
               [counter, clicks]() mutable { counter->setText(String("Clicked: " + std::to_string(++clicks))); });
        auto* flat = button(page, "Flat button", RectF{20, 160, 170, 42});
        flat->setFlat(true);
        auto* disabled = button(page, "Disabled", RectF{20, 215, 170, 42});
        disabled->setEnabled(false);
        break;
    }
    case 1: {
        page = newPage("CheckBox, RadioButton, Switch");
        auto* check = new C::CheckBox(page);
        check->setText("Tristate CheckBox");
        check->setTristate(true);
        check->setGeometry(20, 65, 230, 36);
        const char* choices[] = {"First choice", "Second choice", "Third choice"};
        for (int i = 0; i < 3; ++i) {
            auto* radio = new C::RadioButton(page);
            radio->setText(choices[i]);
            radio->setGeometry(20, 112 + i * 38, 230, 34);
            if (i == 0) radio->setChecked(true);
        }
        auto* toggle = new C::Switch(page);
        toggle->setText("Enabled");
        toggle->setGeometry(20, 235, 180, 34);
        toggle->setChecked(true);
        break;
    }
    case 2: {
        page = newPage("ScrollView");
        auto* scroll = new C::ScrollView(page);
        scroll->setGeometry(12, 56, contentWidth, contentHeight);
        for (int i = 0; i < 18; ++i) {
            rectangle(scroll->contentItem(), RectF{8, 8.0f + i * 42, contentWidth - 32, 34},
                      i % 2 ? Color::rgb(42, 60, 80) : Color::rgb(35, 48, 64));
            label(scroll->contentItem(), "Scrollable row " + std::to_string(i + 1),
                  20, 14.0f + i * 42, 15);
        }
        scroll->setContentSize(contentWidth - 16, 18 * 42 + 12);
        break;
    }
    case 3: {
        page = newPage("SplitView — drag the separator");
        auto* split = new C::SplitView(page);
        split->setGeometry(12, 58, contentWidth, contentHeight);
        auto* left = rectangle(page, RectF{}, Color::rgb(38, 75, 105));
        auto* right = rectangle(page, RectF{}, Color::rgb(80, 55, 90));
        label(left, "Left panel", 16, 16, 18, Color::rgb(255, 255, 255), true);
        label(right, "Right panel", 16, 16, 18, Color::rgb(255, 255, 255), true);
        split->addItem(left);
        split->addItem(right);
        split->setSizes({1, 2});
        break;
    }
    case 4: {
        page = newPage("StackView");
        auto* stack = new C::StackView(page);
        stack->setGeometry(20, 100, contentWidth - 20, contentHeight - 55);
        auto* first = new Item(stack);
        rectangle(first, RectF{0, 0, 300, 180}, Color::rgb(40, 85, 105))->fill(first);
        label(first, "Stack depth: 1", 20, 20, 20, Color::rgb(255, 255, 255), true);
        auto* second = new Item(stack);
        rectangle(second, RectF{0, 0, 300, 180}, Color::rgb(105, 65, 45))->fill(second);
        label(second, "Stack depth: 2", 20, 20, 20, Color::rgb(255, 255, 255), true);
        stack->push(first);
        button(page, "Push", RectF{20, 55, 110, 34}, [stack, second] { stack->push(second); });
        button(page, "Pop", RectF{140, 55, 110, 34}, [stack] { stack->pop(); });
        break;
    }
    case 5: {
        page = newPage("SwipeView");
        auto* swipe = new C::SwipeView(page);
        swipe->setGeometry(20, 105, contentWidth - 20, contentHeight - 60);
        const Color colors[] = {Color::rgb(40, 85, 115), Color::rgb(85, 55, 110), Color::rgb(45, 105, 75)};
        for (int i = 0; i < 3; ++i) {
            auto* swipePage = new Item(swipe);
            rectangle(swipePage, RectF{0, 0, 400, 220}, colors[i])->fill(swipePage);
            label(swipePage, "Swipe page " + std::to_string(i + 1), 20, 20, 20,
                  Color::rgb(255, 255, 255), true);
            swipe->addItem(swipePage);
        }
        button(page, "Previous", RectF{20, 55, 115, 36}, [swipe] {
            swipe->setCurrentIndex(std::max(0, swipe->currentIndex() - 1));
        });
        button(page, "Next", RectF{145, 55, 115, 36}, [swipe] {
            swipe->setCurrentIndex(std::min(swipe->count() - 1, swipe->currentIndex() + 1));
        });
        break;
    }
    case 6: {
        page = newPage("TabBar");
        auto* tabs = new C::TabBar(page);
        tabs->setGeometry(16, 56, contentWidth, 40);
        tabs->addTab("General");
        tabs->addTab("Details");
        tabs->addTab("About");
        std::vector<Item*> panels;
        for (int i = 0; i < 3; ++i) {
            auto* panel = new Item(page);
            panel->setGeometry(16, 104, contentWidth, contentHeight - 48);
            rectangle(panel, RectF{0, 0, contentWidth, contentHeight - 48},
                      Color::rgb(35 + i * 12, 48 + i * 8, 64 + i * 6))->fill(panel);
            label(panel, i == 0 ? "General settings" : i == 1 ? "Detailed information" : "Firmir gallery",
                  18, 18, 18, Color::rgb(255, 255, 255), true);
            panel->setVisible(i == 0);
            panels.push_back(panel);
        }
        tabs->connect<int>(tabs, C::TabBar::signalCurrentIndexChanged,
                           [panels](int current) {
                               for (std::size_t i = 0; i < panels.size(); ++i)
                                   panels[i]->setVisible(static_cast<int>(i) == current);
                           }, ConnectionMode::Direct);
        break;
    }
    case 7: {
        page = newPage("BusyIndicator & ProgressBar");
        auto* busy = new C::BusyIndicator(page);
        busy->setGeometry(24, 65, 64, 64);
        label(page, "Busy", 100, 83, 17);
        auto* progress = new C::ProgressBar(page);
        progress->setGeometry(24, 155, contentWidth - 30, 16);
        progress->setTo(100);
        progress->setValue(35);
        button(page, "Advance", RectF{24, 185, 120, 34}, [progress] {
            progress->setValue(progress->value() >= 100 ? 0 : progress->value() + 10);
        });
        auto* indeterminate = new C::ProgressBar(page);
        indeterminate->setGeometry(24, 240, contentWidth - 30, 16);
        indeterminate->setIndeterminate(true);
        label(page, "Indeterminate", 24, 218, 14);
        break;
    }
    case 8: {
        page = newPage("Font & FontDatabase");
        const int id = FontDatabase::addApplicationFont(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        label(page, "System families: " + std::to_string(FontDatabase::families().size()) +
              "; application font id: " + std::to_string(id), 20, 58, 14);
        auto* normal = label(page, "DejaVu Sans — regular 24 px", 20, 100, 24);
        (void)normal;
        auto* boldText = label(page, "Bold text", 20, 145, 22, Color::rgb(245, 248, 252), true);
        (void)boldText;
        auto* italicText = label(page, "Italic text", 20, 185, 22);
        Font italic = italicText->font();
        italic.setItalic(true);
        italicText->setFont(italic);
        auto* underline = label(page, "Underlined text", 20, 225, 20);
        Font underlineFont = underline->font();
        underlineFont.setUnderline(true);
        underline->setFont(underlineFont);
        label(page, "Unicode: Привет, мир!  Γειά σου  你好", 20, 265, 20,
              Color::rgb(120, 205, 245));
        break;
    }
    case 9: {
        page = newPage("Image — RGBA / PNG / JPEG");
        std::vector<std::uint8_t> pixels(64 * 64 * 4);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const std::size_t offset = (y * 64 + x) * 4;
                pixels[offset + 0] = static_cast<std::uint8_t>(x * 4);
                pixels[offset + 1] = static_cast<std::uint8_t>(y * 4);
                pixels[offset + 2] = static_cast<std::uint8_t>(180 + ((x + y) % 2) * 60);
                pixels[offset + 3] = 255;
            }
        auto* fit = new Image(page);
        fit->setRgba(64, 64, pixels.data(), pixels.size());
        fit->setGeometry(20, 70, 170, 170);
        fit->setFillMode(Image::FillMode::PreserveAspectFit);
        label(page, "PreserveAspectFit", 20, 248, 14);
        auto* crop = new Image(page);
        crop->setRgba(64, 64, pixels.data(), pixels.size());
        crop->setGeometry(220, 70, 170, 170);
        crop->setFillMode(Image::FillMode::PreserveAspectCrop);
        label(page, "PreserveAspectCrop", 220, 248, 14);
        break;
    }
    case 10: {
        page = newPage("ShaderEffect");
        auto* effect = new ShaderEffect(page);
        effect->setGeometry(20, 70, 190, 160);
        effect->setFragmentShader(
            "gl_FragColor = vec4(v_uv.x, 0.25 + 0.5 * v_uv.y, 1.0 - v_uv.x, 1.0) * v_color;");
        label(page, "Custom fragment shader", 20, 240, 14);
        std::vector<std::uint8_t> pixels(16 * 16 * 4);
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i] = 220; pixels[i + 1] = 120; pixels[i + 2] = 40; pixels[i + 3] = 255;
        }
        auto* source = new Image(page);
        source->setRgba(16, 16, pixels.data(), pixels.size());
        source->setVisible(false);
        auto* textured = new ShaderEffect(page);
        textured->setGeometry(240, 70, 190, 160);
        textured->setSourceImage(source);
        textured->setFragmentShader(
            "vec4 c = texture2D(u_texture, v_uv);"
            "gl_FragColor = vec4(c.b, c.g, c.r, c.a) * (0.5 + 0.5 * v_uv.y);");
        label(page, "Textured shader", 240, 240, 14);
        break;
    }
    case 11: {
        page = newPage("MouseArea");
        rectangle(page, RectF{20, 70, contentWidth - 20, contentHeight - 20},
                  Color::rgb(45, 75, 100));
        auto* area = new MouseArea(page);
        area->setGeometry(20, 70, contentWidth - 20, contentHeight - 20);
        area->setHoverEnabled(true);
        auto* status = label(page, "Click or move the pointer", 34, 86, 17);
        area->connect<MouseEvent>(area, MouseArea::signalClicked, [status](MouseEvent event) {
            status->setText(String("Clicked at " + std::to_string(static_cast<int>(event.pos.x)) +
                                   ", " + std::to_string(static_cast<int>(event.pos.y))));
        }, ConnectionMode::Direct);
        area->connect<Point>(area, MouseArea::signalPositionChanged, [status](Point point) {
            status->setText(String("Pointer: " + std::to_string(static_cast<int>(point.x)) +
                                   ", " + std::to_string(static_cast<int>(point.y))));
        }, ConnectionMode::Direct);
        break;
    }
    case 12: {
        page = newPage("Flickable — drag with inertia");
        auto* flickable = new Flickable(page);
        flickable->setGeometry(16, 58, contentWidth, contentHeight);
        Item* content = flickable->contentItem();
        for (int i = 0; i < 16; ++i) {
            rectangle(content, RectF{8, 8.0f + i * 40, contentWidth - 24, 30},
                      Color::rgb(40 + i * 4, 60 + i * 3, 80 + i * 2));
            label(content, "Flickable item " + std::to_string(i + 1), 18, 12.0f + i * 40, 14);
        }
        flickable->setContentWidth(contentWidth - 8);
        flickable->setContentHeight(16 * 40 + 12);
        break;
    }
    // GALLERY_MORE_PAGES
    default:
        return;
    }
    m_stack->push(page);
}

} // namespace

int main(int argc, char**) {
    (void)argc;
    Platform* platform = Platform::instance();
#ifdef FIRMIR_DEMO_HEADLESS
    if (std::getenv("FIRMIR_GALLERY_HEADLESS") && platform->screens().empty())
        firmir_gui_headless_add_screen("GALLERY", 1920, 1080);
#endif
    const auto screens = platform->screens();
    if (screens.empty()) {
        std::fprintf(stderr, "red-window: no display screens\n");
        return 1;
    }
    const Size screen = screens.front()->size();
    const int width = std::max(1, screen.width / 3);
    const int height = std::max(1, screen.height / 3);
    Window window(screens.front());
    window.setTitle("Firmir Controls Gallery");
    window.resize(width, height);
    Gallery gallery(window, width, height);
    if (const char* page = std::getenv("FIRMIR_GALLERY_PAGE")) {
        const int index = std::clamp(std::atoi(page), 0, 12);
        gallery.showPage(index);
    }
    if (std::getenv("FIRMIR_GALLERY_AUTO_QUIT"))
        Timer::singleShot(std::chrono::milliseconds(1500), &window,
                          [&window] { window.close(); });
    window.connect<bool>(&window, Window::signalVisibilityChanged, [](bool visible) {
        if (!visible) Thread::mainThread()->quit();
    }, ConnectionMode::Direct);
    window.show();
    return Thread::mainThread()->exec();
}
