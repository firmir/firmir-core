#include <firmir/file.hpp>

#include <cerrno>
#include <cstring>
#include <sys/stat.h>

namespace Firmir {

struct File::Impl {
    String name;
    std::FILE* handle = nullptr;
    String error;

    ~Impl() {
        if (handle)
            std::fclose(handle);
    }
};

File::File(const String& name) : d(std::make_unique<Impl>()) {
    d->name = name;
}

File::~File() = default;
File::File(File&&) noexcept = default;
File& File::operator=(File&&) noexcept = default;

void File::setFileName(const String& name) {
    if (isOpen())
        close();
    d->name = name;
}

String File::fileName() const { return d->name; }

bool File::open(OpenMode mode) {
    if (isOpen())
        close();
    d->error.clear();

    std::string m;
    const bool read = (mode & ReadOnly) != 0;
    const bool write = (mode & WriteOnly) != 0;
    const bool append = (mode & Append) != 0;
    if (append)
        m = read ? "a+" : "a";
    else if (read && write)
        m = (mode & Truncate) ? "w+" : "r+";
    else if (write)
        m = "w"; // WriteOnly implies Truncate
    else if (read)
        m = "r";
    m += 'b';

    d->handle = std::fopen(d->name.toStdString().c_str(), m.c_str());
    if (!d->handle) {
        d->error = String(std::strerror(errno));
        return false;
    }
    return true;
}

void File::close() {
    if (d->handle) {
        std::fclose(d->handle);
        d->handle = nullptr;
    }
}

bool File::isOpen() const { return d->handle != nullptr; }

void File::flush() {
    if (d->handle)
        std::fflush(d->handle);
}

long long File::pos() const {
    if (!d->handle)
        return 0;
    return static_cast<long long>(std::ftell(d->handle));
}

long long File::size() const {
    if (!d->handle) {
        struct stat st {};
        if (::stat(d->name.toStdString().c_str(), &st) != 0)
            return 0;
        return static_cast<long long>(st.st_size);
    }
    const long long current = pos();
    std::fseek(d->handle, 0, SEEK_END);
    const long long end = pos();
    std::fseek(d->handle, current, SEEK_SET);
    return end;
}

bool File::seek(long long pos) {
    if (!d->handle)
        return false;
    return std::fseek(d->handle, static_cast<long>(pos), SEEK_SET) == 0;
}

bool File::atEnd() const {
    if (!d->handle)
        return true;
    const int c = std::fgetc(d->handle);
    if (c == EOF)
        return true;
    std::ungetc(c, d->handle);
    return false;
}

ByteArray File::readAll() {
    ByteArray out;
    char buf[16384];
    while (d->handle) {
        const std::size_t n = std::fread(buf, 1, sizeof(buf), d->handle);
        if (n == 0)
            break;
        out.append(buf, static_cast<int>(n));
    }
    return out;
}

ByteArray File::read(long long maxSize) {
    ByteArray out;
    if (!d->handle || maxSize <= 0)
        return out;
    out.resize(static_cast<int>(maxSize));
    const std::size_t n =
        std::fread(out.data(), 1, static_cast<std::size_t>(maxSize), d->handle);
    out.resize(static_cast<int>(n));
    return out;
}

long long File::write(const ByteArray& data) {
    if (!d->handle)
        return -1;
    const std::size_t n = std::fwrite(data.constData(), 1,
                                      static_cast<std::size_t>(data.size()),
                                      d->handle);
    return static_cast<long long>(n);
}

String File::errorString() const { return d->error; }

bool File::exists(const String& name) {
    struct stat st {};
    return ::stat(name.toStdString().c_str(), &st) == 0;
}

bool File::remove(const String& name) {
    return std::remove(name.toStdString().c_str()) == 0;
}

} // namespace Firmir
