#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/string.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace Firmir {
namespace Gui {

/// Raster image item with PNG/JPEG loading.
class Image : public Item {
public:
    enum class FillMode { Stretch, PreserveAspectFit, PreserveAspectCrop, Pad };
    enum class Status { Null, Loading, Ready, Error };
    explicit Image(Object* parent = nullptr);
    ~Image() override;

    String source() const;
    void setSource(String source);
    Status status() const;
    Size sourceSize() const;
    FillMode fillMode() const;
    void setFillMode(FillMode mode);
    /// String form for the property system / Firml ("Stretch",
    /// "PreserveAspectFit", "PreserveAspectCrop", "Pad").
    String fillModeName() const;
    void setFillModeName(const String& name);
    bool smooth() const;
    void setSmooth(bool smooth);
    /// Replaces the image with caller-owned content copied as RGBA8.
    void setRgba(int width, int height, const void* pixels, std::size_t bytes);
    /// CPU pixels in RGBA8 order, valid when status()==Ready.
    const std::vector<std::uint8_t>& rgbaData() const;
    std::uint64_t rgbaGeneration() const;
protected:
    void paint(Painter& painter) override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
