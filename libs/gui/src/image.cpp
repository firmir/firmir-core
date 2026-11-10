#include <firmir/gui/image.hpp>

#include <png.h>
#include <jpeglib.h>

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Firmir {
namespace Gui {
namespace {

bool decodePng(const std::string& path, std::vector<std::uint8_t>& rgba, Size& size) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path.c_str()))
        return false;
    image.format = PNG_FORMAT_RGBA;
    const png_alloc_size_t bytes = PNG_IMAGE_SIZE(image);
    rgba.resize(bytes);
    const bool success = png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr) != 0;
    if (success)
        size = {static_cast<int>(image.width), static_cast<int>(image.height)};
    else
        rgba.clear();
    png_image_free(&image);
    return success;
}

struct JpegError {
    jpeg_error_mgr base;
    jmp_buf jump;
};

void jpegErrorExit(j_common_ptr info) {
    auto* error = reinterpret_cast<JpegError*>(info->err);
    longjmp(error->jump, 1);
}

bool decodeJpeg(const std::string& path, std::vector<std::uint8_t>& rgba, Size& size) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return false;
    jpeg_decompress_struct info{};
    JpegError error{};
    info.err = jpeg_std_error(&error.base);
    error.base.error_exit = jpegErrorExit;
    if (setjmp(error.jump)) {
        jpeg_destroy_decompress(&info);
        std::fclose(file);
        return false;
    }
    jpeg_create_decompress(&info);
    jpeg_stdio_src(&info, file);
    jpeg_read_header(&info, TRUE);
    info.out_color_space = JCS_RGB;
    jpeg_start_decompress(&info);
    const std::size_t width = info.output_width;
    const std::size_t height = info.output_height;
    const std::size_t components = info.output_components;
    auto* decoded = static_cast<std::uint8_t*>(std::malloc(width * height * 4));
    auto* row = static_cast<std::uint8_t*>(std::malloc(width * components));
    if (!decoded || !row) {
        std::free(decoded); std::free(row);
        jpeg_destroy_decompress(&info); std::fclose(file);
        return false;
    }
    while (info.output_scanline < info.output_height) {
        JSAMPROW rows[1] = {row};
        jpeg_read_scanlines(&info, rows, 1);
        const std::size_t y = info.output_scanline - 1;
        for (std::size_t x = 0; x < width; ++x) {
            decoded[(y * width + x) * 4 + 0] = row[x * components + 0];
            decoded[(y * width + x) * 4 + 1] = row[x * components + 1];
            decoded[(y * width + x) * 4 + 2] = row[x * components + 2];
            decoded[(y * width + x) * 4 + 3] = 255;
        }
    }
    jpeg_finish_decompress(&info);
    jpeg_destroy_decompress(&info);
    std::fclose(file);
    rgba.assign(decoded, decoded + width * height * 4);
    std::free(decoded);
    std::free(row);
    size = {static_cast<int>(width), static_cast<int>(height)};
    return true;
}

} // namespace

class Image::Impl {
public:
    String source;
    Size sourceSize;
    std::vector<std::uint8_t> rgba;
    std::uint64_t generation = 0;
    FillMode fillMode = FillMode::PreserveAspectFit;
    Status status = Status::Null;
    bool smooth = true;
};

Image::Image(Object* parent) : Item(parent), d(std::make_unique<Impl>()) {
    registerProperty<String>("source", &Image::source, &Image::setSource);
    registerPropertyImpl("fillMode", typeid(Firmir::String),
        [](const Object* o) -> std::any { return static_cast<const Image*>(o)->fillModeName(); },
        [](Object* o, const std::any& v) { static_cast<Image*>(o)->setFillModeName(std::any_cast<Firmir::String>(v)); });
    registerProperty<bool>("smooth", &Image::smooth, &Image::setSmooth);
    registerPropertyImpl("status", typeid(int),
        [](const Object* o) -> std::any { return static_cast<int>(static_cast<const Image*>(o)->status()); },
        [](Object*, const std::any&) { throw std::invalid_argument("status is read-only"); });
}
Image::~Image() = default;
String Image::source() const { return d->source; }
Image::Status Image::status() const { return d->status; }
Size Image::sourceSize() const { return d->sourceSize; }
Image::FillMode Image::fillMode() const { return d->fillMode; }
bool Image::smooth() const { return d->smooth; }
const std::vector<std::uint8_t>& Image::rgbaData() const { return d->rgba; }
std::uint64_t Image::rgbaGeneration() const { return d->generation; }
void Image::setSmooth(bool smooth) {
    auto sceneLock = lockScene();
    d->smooth = smooth;
    update();
}
void Image::setSource(String source) {
    auto sceneLock = lockScene();
    d->source = std::move(source);
    d->rgba.clear();
    d->sourceSize = {};
    if (d->source.isEmpty()) {
        d->status = Status::Null;
    } else {
        d->status = Status::Loading;
        std::string path = d->source.toStdString();
        std::string extension = path;
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool jpeg = extension.size() >= 4 &&
            (extension.compare(extension.size() - 4, 4, ".jpg") == 0 ||
             (extension.size() >= 5 && extension.compare(extension.size() - 5, 5, ".jpeg") == 0));
        const bool loaded = jpeg ? decodeJpeg(path, d->rgba, d->sourceSize)
                                 : decodePng(path, d->rgba, d->sourceSize);
        d->status = loaded ? Status::Ready : Status::Error;
    }
    ++d->generation;
    setImplicitWidth(d->sourceSize.width);
    setImplicitHeight(d->sourceSize.height);
    update();
    notifyPropertyChanged("source");
    notifyPropertyChanged("status");
}
void Image::setFillMode(FillMode mode) {
    auto sceneLock = lockScene();
    if (d->fillMode == mode) return;
    d->fillMode = mode;
    setClip(mode == FillMode::PreserveAspectCrop);
    update();
    notifyPropertyChanged("fillMode");
}
String Image::fillModeName() const {
    switch (d->fillMode) {
    case FillMode::Stretch: return String("Stretch");
    case FillMode::PreserveAspectCrop: return String("PreserveAspectCrop");
    case FillMode::Pad: return String("Pad");
    default: return String("PreserveAspectFit");
    }
}
void Image::setFillModeName(const String& name) {
    const std::string value = name.toStdString();
    if (value == "Stretch") setFillMode(FillMode::Stretch);
    else if (value == "PreserveAspectFit") setFillMode(FillMode::PreserveAspectFit);
    else if (value == "PreserveAspectCrop") setFillMode(FillMode::PreserveAspectCrop);
    else if (value == "Pad") setFillMode(FillMode::Pad);
}
void Image::setRgba(int width, int height, const void* pixels, std::size_t bytes) {
    auto sceneLock = lockScene();
    const std::size_t expected = width > 0 && height > 0
        ? static_cast<std::size_t>(width) * height * 4 : 0;
    if (!pixels || bytes != expected) {
        d->status = Status::Error;
        return;
    }
    const auto* data = static_cast<const std::uint8_t*>(pixels);
    d->rgba.assign(data, data + bytes);
    d->sourceSize = {width, height};
    d->source = {};
    d->status = Status::Ready;
    ++d->generation;
    setImplicitWidth(width);
    setImplicitHeight(height);
    update();
    notifyPropertyChanged("status");
}
void Image::paint(Painter& painter) {
    if (d->status != Status::Ready || d->rgba.empty() || width() <= 0 || height() <= 0)
        return;
    float w = width(), h = height(), x = 0, y = 0;
    const float sx = width() / d->sourceSize.width;
    const float sy = height() / d->sourceSize.height;
    if (d->fillMode == FillMode::PreserveAspectFit || d->fillMode == FillMode::PreserveAspectCrop) {
        const float scale = d->fillMode == FillMode::PreserveAspectFit ? std::min(sx, sy) : std::max(sx, sy);
        w = d->sourceSize.width * scale;
        h = d->sourceSize.height * scale;
        x = (width() - w) / 2;
        y = (height() - h) / 2;
    } else if (d->fillMode == FillMode::Pad) {
        w = d->sourceSize.width;
        h = d->sourceSize.height;
        x = (width() - w) / 2;
        y = (height() - h) / 2;
    }
    painter.drawRgba(RectF{x, y, w, h}, d->rgba.data(), d->sourceSize.width,
                     d->sourceSize.height, this, d->generation);
}
} // namespace Gui
} // namespace Firmir
