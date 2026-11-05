#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/string.hpp>

#include <cstdint>
#include <vector>

namespace Firmir {
namespace Gui {

/// Raster image item with PNG/JPEG loading (QML Image analogue).
class Image : public Item {
public:
    enum class FillMode { Stretch, PreserveAspectFit, PreserveAspectCrop, Pad };
    enum class Status { Null, Loading, Ready, Error };
    explicit Image(Object* parent = nullptr);
    ~Image() override;

    String source() const { return m_source; }
    void setSource(String source);
    Status status() const { return m_status; }
    Size sourceSize() const { return m_sourceSize; }
    FillMode fillMode() const { return m_fillMode; }
    void setFillMode(FillMode mode);
    bool smooth() const { return m_smooth; }
    void setSmooth(bool smooth) { auto lock = lockScene(); m_smooth = smooth; update(); }
    /// Replaces the image with caller-owned content copied as RGBA8.
    void setRgba(int width, int height, const void* pixels, std::size_t bytes);
    /// CPU pixels in RGBA8 order, valid when status()==Ready.
    const std::vector<std::uint8_t>& rgbaData() const { return m_rgba; }
    std::uint64_t rgbaGeneration() const { return m_generation; }
protected:
    void paint(Painter& painter) override;
private:
    String m_source;
    Size m_sourceSize;
    std::vector<std::uint8_t> m_rgba;
    std::uint64_t m_generation = 0;
    FillMode m_fillMode = FillMode::PreserveAspectFit;
    Status m_status = Status::Null;
    bool m_smooth = true;
};

} // namespace Gui
} // namespace Firmir
