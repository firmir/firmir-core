#pragma once

#include <firmir/fileinfo.hpp>
#include <firmir/string.hpp>

#include <memory>
#include <vector>

namespace Firmir {

/// Firmir analogue of QDir: directory traversal and manipulation.
class Dir {
public:
    explicit Dir(const String& path = ".");
    ~Dir();
    Dir(const Dir&);
    Dir(Dir&&) noexcept;
    Dir& operator=(const Dir&);
    Dir& operator=(Dir&&) noexcept;

    String path() const;
    String absolutePath() const;

    bool exists() const;
    bool cd(const String& dirName);
    bool cdUp();

    String filePath(const String& fileName) const;
    String absoluteFilePath(const String& fileName) const;

    /// Names of all entries except "." and "..".
    std::vector<String> entryList() const;
    /// Full metadata for all entries except "." and "..".
    std::vector<FileInfo> entryInfoList() const;

    bool mkpath(const String& dirPath) const;
    bool rmdir(const String& dirName) const;
    bool removeRecursively() const;

    static Dir current();
    static String currentPath();
    static String homePath();
    static char separator() { return '/'; }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
