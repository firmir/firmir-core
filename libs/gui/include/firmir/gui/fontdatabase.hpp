#pragma once

#include <firmir/gui/font.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace Firmir {
namespace Gui {

/// System and application font registry (QFontDatabase analogue).
class FontDatabase {
public:
    FontDatabase() = default;

    static std::vector<std::string> families();
    static std::vector<std::string> styles(const std::string& family);

    /// Returns an application font id, or -1 when the file cannot be loaded.
    static int addApplicationFont(const std::string& fileName);
    /// The data is copied and may describe TTF, OTF or another FreeType format.
    static int addApplicationFontFromData(const void* data, std::size_t bytes);
    static bool removeApplicationFont(int id);
    static void removeAllApplicationFonts();
    static std::vector<std::string> applicationFontFamilies(int id);
};

} // namespace Gui
} // namespace Firmir
