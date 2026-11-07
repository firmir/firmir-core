#include <firmir/textstream.hpp>

#include <cctype>

namespace Firmir {

struct TextStream::Impl {
    String* string = nullptr;
    File* file = nullptr;

    std::string buf;       // read cache (mirrors *string / whole file)
    std::size_t pos = 0;   // read cursor
    bool loaded = false;   // file content cached

    void ensureLoaded() {
        if (loaded)
            return;
        loaded = true;
        if (string)
            buf = string->toStdString();
        else if (file && file->isOpen())
            buf = file->readAll().toStdString();
    }

    void writeRaw(const std::string& s) {
        if (string) {
            buf.append(s);
            string->append(String(s));
        } else if (file && file->isOpen()) {
            file->write(ByteArray(s));
        }
    }

    void skipWs() {
        while (pos < buf.size() &&
               std::isspace(static_cast<unsigned char>(buf[pos])))
            ++pos;
    }
};

TextStream::TextStream(String* string) : d(std::make_unique<Impl>()) {
    d->string = string;
}

TextStream::TextStream(File* file) : d(std::make_unique<Impl>()) {
    d->file = file;
}

TextStream::~TextStream() { flush(); }

TextStream& TextStream::operator<<(const String& value) {
    d->writeRaw(value.toStdString());
    return *this;
}

TextStream& TextStream::operator<<(const char* value) {
    d->writeRaw(value ? value : "");
    return *this;
}

TextStream& TextStream::operator<<(char value) {
    d->writeRaw(std::string(1, value));
    return *this;
}

TextStream& TextStream::operator<<(int value) {
    d->writeRaw(String::number(value).toStdString());
    return *this;
}

TextStream& TextStream::operator<<(long long value) {
    d->writeRaw(String::number(value).toStdString());
    return *this;
}

TextStream& TextStream::operator<<(double value) {
    d->writeRaw(String::number(value).toStdString());
    return *this;
}

TextStream& TextStream::operator>>(String& word) {
    d->ensureLoaded();
    d->skipWs();
    const std::size_t start = d->pos;
    while (d->pos < d->buf.size() &&
           !std::isspace(static_cast<unsigned char>(d->buf[d->pos])))
        ++d->pos;
    word = String(d->buf.substr(start, d->pos - start));
    return *this;
}

String TextStream::readAll() {
    d->ensureLoaded();
    String out(d->buf.substr(d->pos));
    d->pos = d->buf.size();
    return out;
}

String TextStream::readLine() {
    d->ensureLoaded();
    if (d->pos >= d->buf.size())
        return String();
    const std::size_t nl = d->buf.find('\n', d->pos);
    if (nl == std::string::npos) {
        String out(d->buf.substr(d->pos));
        d->pos = d->buf.size();
        return out;
    }
    String out(d->buf.substr(d->pos, nl - d->pos));
    d->pos = nl + 1;
    return out;
}

bool TextStream::atEnd() const {
    // const_cast: atEnd is logically const but lazily loads the cache.
    const_cast<Impl*>(d.get())->ensureLoaded();
    return d->pos >= d->buf.size();
}

void TextStream::flush() {
    if (d->file && d->file->isOpen())
        d->file->flush();
}

} // namespace Firmir
