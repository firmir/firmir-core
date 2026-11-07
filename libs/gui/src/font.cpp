#include <firmir/gui/font.hpp>
#include <firmir/gui/fontdatabase.hpp>
#include "detail/fontengine.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <hb-ft.h>
#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <limits>
#include <mutex>
#include <set>
#include <utility>

namespace Firmir {
namespace Gui {

Font::Font() = default;
Font::Font(std::string family, int pixelSize, int weight, bool italic)
    : m_family(std::move(family)), m_weight(weight) {
    setPixelSize(pixelSize);
    setItalic(italic);
}

void Font::setFamily(std::string family) { m_family = std::move(family); }
void Font::setPixelSize(int pixels) {
    m_pixelSize = std::max(1, pixels);
    m_pointSize = -1.0f;
}
void Font::setPointSize(float points) {
    if (points > 0) {
        m_pointSize = points;
        m_pixelSize = -1;
    }
}
int Font::effectivePixelSize() const {
    if (m_pixelSize > 0)
        return m_pixelSize;
    return std::max(1, static_cast<int>(std::lround(m_pointSize * 96.0f / 72.0f)));
}
std::string Font::cacheKey() const {
    return m_family + '\n' + m_styleName + '\n' + std::to_string(effectivePixelSize()) +
           '\n' + std::to_string(m_weight) + '\n' + std::to_string(m_style) + '\n' +
           std::to_string(m_letterSpacing) + '\n' + std::to_string(m_underline) +
           '\n' + std::to_string(m_strikeOut);
}

namespace {

struct ApplicationFont {
    int id = -1;
    std::string path;
    std::shared_ptr<std::vector<std::uint8_t>> bytes;
    std::vector<std::string> families;
    std::vector<std::string> styles;
};

struct Database {
    Database() { FT_Init_FreeType(&library); }
    ~Database() { if (library) FT_Done_FreeType(library); FcFini(); }
    std::mutex mutex;
    FT_Library library = nullptr;
    std::vector<ApplicationFont> applicationFonts;
    int nextId = 0;
};

Database& database() {
    static Database value;
    return value;
}

bool inspectFace(FT_Face face, ApplicationFont& entry) {
    if (!face || !face->family_name)
        return false;
    entry.families.emplace_back(face->family_name);
    entry.styles.emplace_back(face->style_name ? face->style_name : "Regular");
    return true;
}

void rebuildApplicationFonts(Database& db) {
    FcConfig* config = FcConfigGetCurrent();
    FcConfigAppFontClear(config);
    for (const ApplicationFont& entry : db.applicationFonts)
        if (!entry.path.empty())
            FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(entry.path.c_str()));
}

} // namespace

std::vector<std::string> FontDatabase::families() {
    auto& db = database();
    std::lock_guard<std::mutex> lock(db.mutex);
    std::set<std::string> result;
    FcPattern* pattern = FcPatternCreate();
    FcObjectSet* objects = FcObjectSetBuild(FC_FAMILY, nullptr);
    FcFontSet* fonts = FcFontList(FcConfigGetCurrent(), pattern, objects);
    if (fonts) {
        for (int i = 0; i < fonts->nfont; ++i) {
            FcChar8* family = nullptr;
            if (FcPatternGetString(fonts->fonts[i], FC_FAMILY, 0, &family) == FcResultMatch && family)
                result.insert(reinterpret_cast<const char*>(family));
        }
        FcFontSetDestroy(fonts);
    }
    if (objects) FcObjectSetDestroy(objects);
    if (pattern) FcPatternDestroy(pattern);
    for (const auto& entry : db.applicationFonts)
        result.insert(entry.families.begin(), entry.families.end());
    return {result.begin(), result.end()};
}

std::vector<std::string> FontDatabase::styles(const std::string& family) {
    auto& db = database();
    std::lock_guard<std::mutex> lock(db.mutex);
    std::set<std::string> result;
    FcPattern* pattern = FcPatternCreate();
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));
    FcObjectSet* objects = FcObjectSetBuild(FC_STYLE, nullptr);
    FcFontSet* fonts = FcFontList(FcConfigGetCurrent(), pattern, objects);
    if (fonts) {
        for (int i = 0; i < fonts->nfont; ++i) {
            FcChar8* style = nullptr;
            if (FcPatternGetString(fonts->fonts[i], FC_STYLE, 0, &style) == FcResultMatch && style)
                result.insert(reinterpret_cast<const char*>(style));
        }
        FcFontSetDestroy(fonts);
    }
    if (objects) FcObjectSetDestroy(objects);
    if (pattern) FcPatternDestroy(pattern);
    for (const auto& entry : db.applicationFonts)
        if (std::find(entry.families.begin(), entry.families.end(), family) != entry.families.end())
            result.insert(entry.styles.begin(), entry.styles.end());
    return {result.begin(), result.end()};
}

int FontDatabase::addApplicationFont(const std::string& fileName) {
    auto& db = database();
    std::lock_guard<std::mutex> lock(db.mutex);
    if (!db.library)
        return -1;
    ApplicationFont entry;
    FT_Face face = nullptr;
    if (FT_New_Face(db.library, fileName.c_str(), 0, &face) != 0)
        return -1;
    const bool valid = inspectFace(face, entry);
    FT_Done_Face(face);
    if (!valid || !FcConfigAppFontAddFile(FcConfigGetCurrent(),
            reinterpret_cast<const FcChar8*>(fileName.c_str())))
        return -1;
    entry.id = db.nextId++;
    entry.path = fileName;
    db.applicationFonts.push_back(std::move(entry));
    return db.applicationFonts.back().id;
}

int FontDatabase::addApplicationFontFromData(const void* data, std::size_t bytes) {
    auto& db = database();
    std::lock_guard<std::mutex> lock(db.mutex);
    if (!db.library || !data || bytes == 0 || bytes > static_cast<std::size_t>(std::numeric_limits<FT_Long>::max()))
        return -1;
    ApplicationFont entry;
    entry.bytes = std::make_shared<std::vector<std::uint8_t>>(
        static_cast<const std::uint8_t*>(data), static_cast<const std::uint8_t*>(data) + bytes);
    FT_Face face = nullptr;
    if (FT_New_Memory_Face(db.library, entry.bytes->data(), static_cast<FT_Long>(bytes), 0, &face) != 0)
        return -1;
    const bool valid = inspectFace(face, entry);
    FT_Done_Face(face);
    if (!valid)
        return -1;
    entry.id = db.nextId++;
    db.applicationFonts.push_back(std::move(entry));
    return db.applicationFonts.back().id;
}

bool FontDatabase::removeApplicationFont(int id) {
    auto& db = database();
    std::lock_guard<std::mutex> lock(db.mutex);
    const auto found = std::find_if(db.applicationFonts.begin(), db.applicationFonts.end(),
        [id](const ApplicationFont& entry) { return entry.id == id; });
    if (found == db.applicationFonts.end())
        return false;
    db.applicationFonts.erase(found);
    rebuildApplicationFonts(db);
    return true;
}

void FontDatabase::removeAllApplicationFonts() {
    auto& db = database();
    std::lock_guard<std::mutex> lock(db.mutex);
    db.applicationFonts.clear();
    rebuildApplicationFonts(db);
}

std::vector<std::string> FontDatabase::applicationFontFamilies(int id) {
    auto& db = database();
    std::lock_guard<std::mutex> lock(db.mutex);
    for (const auto& entry : db.applicationFonts)
        if (entry.id == id)
            return entry.families;
    return {};
}

namespace {

struct ResolvedFont {
    std::string path;
    std::shared_ptr<std::vector<std::uint8_t>> bytes;
    int index = 0;
};

ResolvedFont resolveFont(Database& db, const Font& font) {
    for (const auto& entry : db.applicationFonts) {
        if (std::find(entry.families.begin(), entry.families.end(), font.family()) != entry.families.end())
            return {entry.path, entry.bytes, 0};
    }
    ResolvedFont result;
    FcPattern* pattern = FcPatternCreate();
    const std::string family = font.family().empty() ? kDefaultFontFamily : font.family();
    FcPatternAddString(pattern, FC_FAMILY, reinterpret_cast<const FcChar8*>(family.c_str()));
    if (!font.styleName().empty())
        FcPatternAddString(pattern, FC_STYLE, reinterpret_cast<const FcChar8*>(font.styleName().c_str()));
    FcPatternAddInteger(pattern, FC_WEIGHT, std::clamp(font.weight() * 210 / 1000, 0, 210));
    FcPatternAddInteger(pattern, FC_SLANT, font.italic() ? FC_SLANT_ITALIC : FC_SLANT_ROMAN);
    FcPatternAddDouble(pattern, FC_PIXEL_SIZE, font.effectivePixelSize());
    FcConfigSubstitute(FcConfigGetCurrent(), pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult matchResult = FcResultNoMatch;
    FcPattern* match = FcFontMatch(FcConfigGetCurrent(), pattern, &matchResult);
    if (match) {
        FcChar8* file = nullptr;
        int index = 0;
        if (FcPatternGetString(match, FC_FILE, 0, &file) == FcResultMatch && file) {
            result.path = reinterpret_cast<const char*>(file);
            FcPatternGetInteger(match, FC_INDEX, 0, &index);
            result.index = index;
        }
        FcPatternDestroy(match);
    }
    FcPatternDestroy(pattern);
    return result;
}

struct ShapedLine {
    std::vector<hb_glyph_info_t> glyphs;
    std::vector<hb_glyph_position_t> positions;
    float width = 0;
};

ShapedLine shapeLine(hb_font_t* font, const std::string& text, float letterSpacing) {
    hb_buffer_t* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, text.data(), static_cast<int>(text.size()), 0,
                       static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buffer);
    hb_shape(font, buffer, nullptr, 0);
    unsigned count = 0;
    hb_glyph_info_t* glyphs = hb_buffer_get_glyph_infos(buffer, &count);
    hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
    ShapedLine result;
    result.glyphs.assign(glyphs, glyphs + count);
    result.positions.assign(positions, positions + count);
    for (unsigned i = 0; i < count; ++i) {
        result.width += result.positions[i].x_advance / 64.0f;
        if (i + 1 < count)
            result.width += letterSpacing;
    }
    hb_buffer_destroy(buffer);
    return result;
}

void drawDecoration(detail::RasterizedText& output, int x0, int x1, int y, int thickness) {
    if (output.width <= 0 || output.height <= 0)
        return;
    const int top = std::clamp(y, 0, output.height - 1);
    const int bottom = std::clamp(y + std::max(1, thickness), 0, output.height);
    for (int py = top; py < bottom; ++py)
        for (int px = std::max(0, x0); px < std::min(output.width, x1); ++px) {
            output.rgba[(static_cast<std::size_t>(py) * output.width + px) * 4 + 0] = 255;
            output.rgba[(static_cast<std::size_t>(py) * output.width + px) * 4 + 1] = 255;
            output.rgba[(static_cast<std::size_t>(py) * output.width + px) * 4 + 2] = 255;
            output.rgba[(static_cast<std::size_t>(py) * output.width + px) * 4 + 3] = 255;
        }
}

} // namespace

RasterizedText rasterizeText(const std::string& utf8, const Font& font) {
    RasterizedText output;
    if (utf8.empty()) return output;
    auto& db = database();
    std::lock_guard<std::mutex> lock(db.mutex);
    if (!db.library) return output;
    const ResolvedFont resolved = resolveFont(db, font);
    FT_Face face = nullptr;
    const FT_Error error = resolved.bytes
        ? FT_New_Memory_Face(db.library, resolved.bytes->data(),
                             static_cast<FT_Long>(resolved.bytes->size()), resolved.index, &face)
        : resolved.path.empty() ? FT_Err_Cannot_Open_Resource
                                : FT_New_Face(db.library, resolved.path.c_str(), resolved.index, &face);
    if (error != 0 || !face) return output;
    if (FT_Set_Pixel_Sizes(face, 0, font.effectivePixelSize()) != 0) {
        FT_Done_Face(face);
        return output;
    }
    hb_font_t* hbFont = hb_ft_font_create_referenced(face);
    if (!hbFont) { FT_Done_Face(face); return output; }
    hb_ft_font_set_load_flags(hbFont, FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL);
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (;;) {
        const std::size_t newline = utf8.find('\n', start);
        lines.push_back(utf8.substr(start, newline == std::string::npos ? newline : newline - start));
        if (newline == std::string::npos) break;
        start = newline + 1;
    }
    std::vector<ShapedLine> shaped;
    float maximumWidth = 0;
    for (const std::string& line : lines) {
        shaped.push_back(shapeLine(hbFont, line, font.letterSpacing()));
        maximumWidth = std::max(maximumWidth, shaped.back().width);
    }
    const int lineHeight = std::max(1, static_cast<int>(std::ceil(face->size->metrics.height / 64.0f)));
    const int ascender = static_cast<int>(std::ceil(face->size->metrics.ascender / 64.0f));
    output.width = std::max(1, static_cast<int>(std::ceil(maximumWidth)) + 2);
    output.height = std::max(1, lineHeight * static_cast<int>(lines.size()) + 2);
    output.rgba.assign(static_cast<std::size_t>(output.width) * output.height * 4, 0);
    for (std::size_t lineIndex = 0; lineIndex < shaped.size(); ++lineIndex) {
        const ShapedLine& line = shaped[lineIndex];
        float penX = 1.0f;
        const float baseline = 1.0f + ascender + lineHeight * static_cast<int>(lineIndex);
        for (std::size_t i = 0; i < line.glyphs.size(); ++i) {
            const hb_glyph_info_t& glyph = line.glyphs[i];
            const hb_glyph_position_t& position = line.positions[i];
            if (FT_Load_Glyph(face, glyph.codepoint, FT_LOAD_DEFAULT | FT_LOAD_TARGET_NORMAL) == 0 &&
                FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) == 0) {
                const FT_Bitmap& bitmap = face->glyph->bitmap;
                const int left = static_cast<int>(std::lround(
                    penX + position.x_offset / 64.0f + face->glyph->bitmap_left));
                const int top = static_cast<int>(std::lround(
                    baseline - position.y_offset / 64.0f - face->glyph->bitmap_top));
                for (unsigned row = 0; row < bitmap.rows; ++row) {
                    const int py = top + static_cast<int>(row);
                    if (py < 0 || py >= output.height) continue;
                    const std::uint8_t* source = bitmap.pitch >= 0
                        ? bitmap.buffer + row * bitmap.pitch
                        : bitmap.buffer + (bitmap.rows - 1 - row) * (-bitmap.pitch);
                    for (unsigned column = 0; column < bitmap.width; ++column) {
                        const int px = left + static_cast<int>(column);
                        if (px < 0 || px >= output.width) continue;
                        const std::uint8_t alpha = source[column];
                        if (!alpha) continue;
                        const std::size_t offset =
                            (static_cast<std::size_t>(py) * output.width + px) * 4;
                        output.rgba[offset + 0] = 255;
                        output.rgba[offset + 1] = 255;
                        output.rgba[offset + 2] = 255;
                        output.rgba[offset + 3] = std::max(output.rgba[offset + 3], alpha);
                    }
                }
            }
            penX += position.x_advance / 64.0f;
            if (i + 1 < line.glyphs.size()) penX += font.letterSpacing();
        }
        if (font.underline())
            drawDecoration(output, 1, output.width - 1, static_cast<int>(baseline) + 1,
                           std::max(1, font.effectivePixelSize() / 16));
        if (font.strikeOut())
            drawDecoration(output, 1, output.width - 1,
                           static_cast<int>(baseline) - std::max(1, ascender / 2),
                           std::max(1, font.effectivePixelSize() / 16));
    }
    output.valid = true;
    hb_font_destroy(hbFont);
    FT_Done_Face(face);
    return output;
}

} // namespace Gui
} // namespace Firmir
