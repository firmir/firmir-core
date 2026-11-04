#pragma once

#include <firmir/bytearray.hpp>
#include <firmir/string.hpp>

#include <cstdio>
#include <memory>

namespace Firmir {

/// Firmir analogue of QFile: sequential file I/O with Qt-style open modes.
class File {
public:
    enum OpenModeFlag : unsigned {
        NotOpen = 0,
        ReadOnly = 0x1,
        WriteOnly = 0x2,
        Append = 0x4,
        Truncate = 0x8,
        Text = 0x10 // accepted for API compatibility; no-op on Linux
    };
    using OpenMode = unsigned;

    File() = default;
    explicit File(const String& name);
    ~File();

    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&&) noexcept;
    File& operator=(File&&) noexcept;

    void setFileName(const String& name);
    String fileName() const;

    bool open(OpenMode mode);
    void close();
    bool isOpen() const;
    void flush();

    long long size() const;
    long long pos() const;
    bool seek(long long pos);
    bool atEnd() const;

    ByteArray readAll();
    ByteArray read(long long maxSize);
    long long write(const ByteArray& data);

    String errorString() const;

    static bool exists(const String& name);
    static bool remove(const String& name);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
