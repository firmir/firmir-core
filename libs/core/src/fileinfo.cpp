#include <firmir/fileinfo.hpp>

#include <filesystem>

namespace Firmir {

namespace fs = std::filesystem;

namespace {

fs::path toFs(const String& p) { return fs::path(p.toStdString()); }

} // namespace

struct FileInfo::Impl {
    String path;
};

FileInfo::FileInfo(const String& path) : d(std::make_unique<Impl>()) {
    d->path = path;
}
FileInfo::~FileInfo() = default;
FileInfo::FileInfo(const FileInfo& other) : d(std::make_unique<Impl>(*other.d)) {}
FileInfo::FileInfo(FileInfo&&) noexcept = default;
FileInfo& FileInfo::operator=(const FileInfo& other) {
    if (this != &other)
        d->path = other.d->path;
    return *this;
}
FileInfo& FileInfo::operator=(FileInfo&&) noexcept = default;

bool FileInfo::exists() const {
    std::error_code ec;
    return fs::exists(toFs(d->path), ec);
}

bool FileInfo::isFile() const {
    std::error_code ec;
    return fs::is_regular_file(toFs(d->path), ec);
}

bool FileInfo::isDir() const {
    std::error_code ec;
    return fs::is_directory(toFs(d->path), ec);
}

bool FileInfo::isSymLink() const {
    std::error_code ec;
    return fs::is_symlink(toFs(d->path), ec);
}

String FileInfo::fileName() const {
    return String(toFs(d->path).filename().string());
}

String FileInfo::baseName() const {
    const std::string name = toFs(d->path).filename().string();
    const std::size_t dot = name.find('.');
    // Hidden files like ".gitignore" have an empty base name: the base name
    // is empty when the first dot is at position 0.
    return String(dot == std::string::npos ? name : name.substr(0, dot));
}

String FileInfo::suffix() const {
    const std::string name = toFs(d->path).filename().string();
    const std::size_t dot = name.rfind('.');
    if (dot == std::string::npos || dot == name.size() - 1)
        return String();
    return String(name.substr(dot + 1));
}

String FileInfo::path() const {
    const std::string parent = toFs(d->path).parent_path().string();
    return String(parent.empty() ? "." : parent);
}

String FileInfo::absoluteFilePath() const {
    std::error_code ec;
    const fs::path abs = fs::absolute(toFs(d->path), ec);
    return String(ec ? d->path.toStdString() : abs.lexically_normal().string());
}

String FileInfo::absolutePath() const {
    return FileInfo(FileInfo(d->path).absoluteFilePath()).path();
}

long long FileInfo::size() const {
    std::error_code ec;
    const auto sz = fs::file_size(toFs(d->path), ec);
    return ec ? 0 : static_cast<long long>(sz);
}

std::chrono::system_clock::time_point FileInfo::lastModified() const {
    std::error_code ec;
    const auto t = fs::last_write_time(toFs(d->path), ec);
    if (ec)
        return {};
    // C++17 lacks clock_cast; approximate via a common epoch shift.
    const auto nowFile = fs::file_time_type::clock::now();
    const auto nowSys = std::chrono::system_clock::now();
    return std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        t - nowFile + nowSys);
}

} // namespace Firmir
