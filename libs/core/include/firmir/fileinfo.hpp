#pragma once

#include <firmir/string.hpp>

#include <chrono>
#include <memory>

namespace Firmir {

/// File metadata queries (size, timestamps, name parts).
class FileInfo {
public:
    explicit FileInfo(const String& path);
    ~FileInfo();
    FileInfo(const FileInfo&);
    FileInfo(FileInfo&&) noexcept;
    FileInfo& operator=(const FileInfo&);
    FileInfo& operator=(FileInfo&&) noexcept;

    bool exists() const;
    bool isFile() const;
    bool isDir() const;
    bool isSymLink() const;

    String fileName() const;     // "archive.tar.gz"
    String baseName() const;     // "archive"
    String suffix() const;       // "gz"
    String path() const;         // directory part as passed
    String absolutePath() const;
    String absoluteFilePath() const;

    long long size() const;
    std::chrono::system_clock::time_point lastModified() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
