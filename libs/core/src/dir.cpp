#include <firmir/dir.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace Firmir {

namespace fs = std::filesystem;

namespace {

fs::path toFs(const String& p) { return fs::path(p.toStdString()); }

} // namespace

struct Dir::Impl {
    String path;
};

Dir::Dir(const String& path) : d(std::make_unique<Impl>()) { d->path = path; }
Dir::~Dir() = default;
Dir::Dir(const Dir& other) : d(std::make_unique<Impl>(*other.d)) {}
Dir::Dir(Dir&&) noexcept = default;
Dir& Dir::operator=(const Dir& other) {
    if (this != &other)
        d->path = other.d->path;
    return *this;
}
Dir& Dir::operator=(Dir&&) noexcept = default;

String Dir::path() const { return d->path; }

String Dir::absolutePath() const {
    std::error_code ec;
    const fs::path abs = fs::absolute(toFs(d->path), ec);
    return String(ec ? d->path.toStdString() : abs.lexically_normal().string());
}

bool Dir::exists() const {
    std::error_code ec;
    return fs::is_directory(toFs(d->path), ec);
}

bool Dir::cd(const String& dirName) {
    // Normalize so that repeated cdUp() does not accumulate ".." components
    // (a lexical "a/b/.." breaks once "a/b" is removed from disk).
    Dir next(String((toFs(d->path) / dirName.toStdString())
                        .lexically_normal()
                        .string()));
    if (!next.exists())
        return false;
    *this = next;
    return true;
}

bool Dir::cdUp() { return cd(".."); }

String Dir::filePath(const String& fileName) const {
    return String((toFs(d->path) / fileName.toStdString()).string());
}

String Dir::absoluteFilePath(const String& fileName) const {
    return String((toFs(absolutePath()) / fileName.toStdString())
                      .lexically_normal()
                      .string());
}

std::vector<String> Dir::entryList() const {
    std::vector<String> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(toFs(d->path), ec)) {
        const std::string name = entry.path().filename().string();
        if (name != "." && name != "..")
            out.emplace_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<FileInfo> Dir::entryInfoList() const {
    std::vector<FileInfo> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(toFs(d->path), ec)) {
        const std::string name = entry.path().filename().string();
        if (name != "." && name != "..")
            out.emplace_back(String(entry.path().string()));
    }
    return out;
}

bool Dir::mkpath(const String& dirPath) const {
    std::error_code ec;
    return fs::create_directories(toFs(filePath(dirPath)), ec) ||
           fs::is_directory(toFs(filePath(dirPath)));
}

bool Dir::rmdir(const String& dirName) const {
    std::error_code ec;
    return fs::remove(toFs(filePath(dirName)), ec);
}

bool Dir::removeRecursively() const {
    std::error_code ec;
    fs::remove_all(toFs(d->path), ec);
    return !ec;
}

Dir Dir::current() { return Dir(currentPath()); }

String Dir::currentPath() {
    std::error_code ec;
    const fs::path p = fs::current_path(ec);
    return String(ec ? "." : p.string());
}

String Dir::homePath() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
    return String(home ? home : "C:/");
#else
    const char* home = std::getenv("HOME");
    return String(home ? home : "/");
#endif
}

} // namespace Firmir
