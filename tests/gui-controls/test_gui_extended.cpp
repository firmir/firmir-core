#include "test_framework.hpp"

#include <firmir/gui-controls/controls.hpp>
#include <firmir/gui/fontdatabase.hpp>
#include <firmir/gui/flickable.hpp>
#include <firmir/gui/image.hpp>
#include <firmir/gui/mousearea.hpp>
#include <firmir/gui/shadereffect.hpp>
#include <firmir/gui/text.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

using namespace Firmir;
using namespace Firmir::Gui;

namespace {

bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

/// Locates a font file usable with FontDatabase::addApplicationFont().
/// FIRMIR_TEST_FONT overrides the search; otherwise the paths used by the common
/// distributions are probed and, failing that, the font roots are scanned for
/// any *.ttf/*.otf, so the test does not depend on one distribution's naming
/// (deb/rpm/arch layouts, minimal build roots, macOS, Windows).
std::string findFontFile() {
    if (const char* env = std::getenv("FIRMIR_TEST_FONT")) {
        if (*env && fileExists(env))
            return env;
        std::fprintf(stderr, "[ WARN ] FIRMIR_TEST_FONT=%s is not a readable file\n", env);
    }

    std::vector<std::string> candidates;
#ifdef _WIN32
    // Windows has no DejaVu by default; Arial ships with every installation.
    const char* winDir = std::getenv("WINDIR");
    const std::string fontDir =
        (winDir && *winDir ? std::string(winDir) : std::string("C:/Windows")) + "/Fonts";
    for (const char* name : {"arial.ttf", "segoeui.ttf", "tahoma.ttf", "verdana.ttf"})
        candidates.push_back(fontDir + "/" + name);
#elif defined(__APPLE__)
    for (const char* path : {"/System/Library/Fonts/Supplemental/Arial.ttf",
                             "/Library/Fonts/Arial.ttf",
                             "/System/Library/Fonts/Supplemental/Verdana.ttf"})
        candidates.emplace_back(path);
#else
    for (const char* path : {
             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",       // Debian, Ubuntu
             "/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf",     // Fedora, RHEL, openSUSE
             "/usr/share/fonts/dejavu/DejaVuSans.ttf",                // Alpine, Arch
             "/usr/share/fonts/TTF/DejaVuSans.ttf",
             "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
             "/usr/share/fonts/liberation-sans-fonts/LiberationSans-Regular.ttf",
             "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf"})
        candidates.emplace_back(path);
#endif
    for (const std::string& path : candidates)
        if (fileExists(path))
            return path;

    std::vector<std::string> roots;
#ifdef _WIN32
    roots.push_back(fontDir);
#elif defined(__APPLE__)
    roots.insert(roots.end(), {"/System/Library/Fonts", "/Library/Fonts"});
#else
    roots.insert(roots.end(),
                 {"/usr/share/fonts", "/usr/local/share/fonts", "/run/host/fonts"});
#endif
    for (const std::string& root : roots) {
        std::error_code ec;
        if (!std::filesystem::is_directory(root, ec) || ec)
            continue;
        std::vector<std::string> found;
        std::error_code iterError;
        std::filesystem::recursive_directory_iterator it(
            root, std::filesystem::directory_options::skip_permission_denied, iterError);
        const std::filesystem::recursive_directory_iterator end;
        while (!iterError && it != end) {
            std::error_code entryError;
            if (it->is_regular_file(entryError) && !entryError) {
                const std::string extension = it->path().extension().string();
                if (extension == ".ttf" || extension == ".otf")
                    found.push_back(it->path().string());
            }
            if (found.size() >= 64)
                break;
            it.increment(iterError);
        }
        if (!found.empty()) {
            std::sort(found.begin(), found.end());
            return found.front();
        }
    }
    return {};
}

} // namespace

TEST_CASE(gui_native_font_and_database) {
    Font font("DejaVu Sans", 24);
    font.setBold(true);
    font.setUnderline(true);
    Text text;
    text.setFont(font);
    text.setText("Привет, Firmir");
    CHECK_EQ(text.fontPixelSize(), 24);
    CHECK(text.fontBold());
    CHECK(text.fontUnderline());
    text.setFontFamily("DejaVu Serif");
    CHECK_EQ(text.fontFamily().toStdString(), "DejaVu Serif");
    text.setProperty("font.italic", true);
    CHECK(text.fontItalic());
    // Metrics need a font the platform can resolve: minimal build roots
    // (rpmbuild/scratchbox/debootstrapped containers) may ship none at all.
    if (FontDatabase::families().empty())
        std::fprintf(stderr, "[ SKIP ] no system fonts installed, text metrics not checked\n");
    else {
        CHECK(text.width() > 20);
        CHECK(text.height() >= 20);
    }
    const std::string fontPath = findFontFile();
    if (fontPath.empty()) {
        std::fprintf(stderr, "[ SKIP ] no font file found, application fonts not checked\n");
        return;
    }
    std::fprintf(stderr, "[ INFO ] application font: %s\n", fontPath.c_str());
    const int id = FontDatabase::addApplicationFont(fontPath);
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
