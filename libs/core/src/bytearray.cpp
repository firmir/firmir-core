#include <firmir/bytearray.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace Firmir {

namespace {

const char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

int hexValue(unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

} // namespace

struct ByteArray::Impl {
    std::string data;
};

ByteArray::ByteArray() : d(std::make_unique<Impl>()) {}
ByteArray::~ByteArray() = default;
ByteArray::ByteArray(const ByteArray& other)
    : d(std::make_unique<Impl>(*other.d)) {}
ByteArray::ByteArray(ByteArray&& other) noexcept = default;
ByteArray& ByteArray::operator=(const ByteArray& other) {
    if (this != &other)
        d->data = other.d->data;
    return *this;
}
ByteArray& ByteArray::operator=(ByteArray&& other) noexcept = default;

ByteArray::ByteArray(const char* data, int size) : d(std::make_unique<Impl>()) {
    if (!data)
        return;
    if (size < 0)
        d->data.assign(data);
    else
        d->data.assign(data, static_cast<std::size_t>(size));
}

ByteArray::ByteArray(const std::string& data) : d(std::make_unique<Impl>()) {
    d->data = data;
}

ByteArray::ByteArray(int size, char ch) : d(std::make_unique<Impl>()) {
    d->data.assign(static_cast<std::size_t>(std::max(0, size)), ch);
}

int ByteArray::size() const { return static_cast<int>(d->data.size()); }
bool ByteArray::isEmpty() const { return d->data.empty(); }
void ByteArray::clear() { d->data.clear(); }
void ByteArray::resize(int size) {
    d->data.resize(static_cast<std::size_t>(std::max(0, size)));
}

char ByteArray::at(int i) const { return d->data.at(static_cast<std::size_t>(i)); }

char* ByteArray::data() { return d->data.data(); }
const char* ByteArray::data() const { return d->data.data(); }

std::string ByteArray::toStdString() const { return d->data; }

bool operator==(const ByteArray& a, const ByteArray& b) {
    return a.d->data == b.d->data;
}
bool operator<(const ByteArray& a, const ByteArray& b) {
    return a.d->data < b.d->data;
}

ByteArray& ByteArray::append(const char* data, int size) {
    if (data && size > 0)
        d->data.append(data, static_cast<std::size_t>(size));
    return *this;
}

ByteArray& ByteArray::append(const ByteArray& other) {
    d->data += other.d->data;
    return *this;
}

ByteArray& ByteArray::prepend(const ByteArray& other) {
    d->data.insert(0, other.d->data);
    return *this;
}

ByteArray& ByteArray::operator+=(char ch) {
    d->data.push_back(ch);
    return *this;
}

int ByteArray::indexOf(const ByteArray& needle, int from) const {
    if (from < 0 || needle.d->data.empty() ||
        static_cast<std::size_t>(from) >= d->data.size())
        return -1;
    const std::size_t pos = d->data.find(needle.d->data, static_cast<std::size_t>(from));
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

bool ByteArray::startsWith(const ByteArray& prefix) const {
    return prefix.d->data.size() <= d->data.size() &&
           d->data.compare(0, prefix.d->data.size(), prefix.d->data) == 0;
}

bool ByteArray::endsWith(const ByteArray& suffix) const {
    return suffix.d->data.size() <= d->data.size() &&
           d->data.compare(d->data.size() - suffix.d->data.size(),
                         suffix.d->data.size(), suffix.d->data) == 0;
}

ByteArray ByteArray::mid(int pos, int length) const {
    const int sz = size();
    if (pos < 0)
        pos = 0;
    if (pos >= sz)
        return ByteArray();
    if (length < 0 || pos + length > sz)
        length = sz - pos;
    ByteArray out;
    out.d->data = d->data.substr(static_cast<std::size_t>(pos),
                             static_cast<std::size_t>(length));
    return out;
}

ByteArray ByteArray::left(int n) const { return mid(0, std::max(0, n)); }

ByteArray ByteArray::right(int n) const {
    if (n <= 0)
        return ByteArray();
    return mid(std::max(0, size() - n));
}
ByteArray ByteArray::trimmed() const {
    std::size_t first = 0;
    std::size_t last = d->data.size();
    auto space = [](char c) {
        return std::isspace(static_cast<unsigned char>(c)) != 0;
    };
    while (first < last && space(d->data[first]))
        ++first;
    while (last > first && space(d->data[last - 1]))
        --last;
    ByteArray out;
    out.d->data = d->data.substr(first, last - first);
    return out;
}

ByteArray& ByteArray::remove(int pos, int length) {
    if (pos < 0 || length <= 0 || pos >= size())
        return *this;
    d->data.erase(static_cast<std::size_t>(pos), static_cast<std::size_t>(length));
    return *this;
}

int ByteArray::toInt(bool* ok, int base) const {
    const char* begin = d->data.c_str();
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(begin, &end, base);
    const bool good = end != begin && *end == '\0' && errno == 0;
    if (ok)
        *ok = good;
    return good ? static_cast<int>(value) : 0;
}

double ByteArray::toDouble(bool* ok) const {
    const char* begin = d->data.c_str();
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(begin, &end);
    const bool good = end != begin && *end == '\0' && errno == 0;
    if (ok)
        *ok = good;
    return good ? value : 0.0;
}

ByteArray ByteArray::toBase64() const {
    ByteArray out;
    std::string& dst = out.d->data;
    dst.reserve((d->data.size() + 2) / 3 * 4);
    const auto* bytes = reinterpret_cast<const unsigned char*>(d->data.data());
    const std::size_t n = d->data.size();
    for (std::size_t i = 0; i < n; i += 3) {
        const unsigned int b0 = bytes[i];
        const unsigned int b1 = i + 1 < n ? bytes[i + 1] : 0;
        const unsigned int b2 = i + 2 < n ? bytes[i + 2] : 0;
        dst.push_back(kBase64Alphabet[b0 >> 2]);
        dst.push_back(kBase64Alphabet[((b0 & 0x3) << 4) | (b1 >> 4)]);
        dst.push_back(i + 1 < n ? kBase64Alphabet[((b1 & 0xF) << 2) | (b2 >> 6)]
                                : '=');
        dst.push_back(i + 2 < n ? kBase64Alphabet[b2 & 0x3F] : '=');
    }
    return out;
}

ByteArray ByteArray::fromBase64(const ByteArray& encoded) {
    ByteArray out;
    std::string& dst = out.d->data;
    dst.reserve(encoded.size() / 4 * 3);
    unsigned int accumulator = 0;
    int bits = 0;
    for (const char ch : encoded.d->data) {
        if (ch == '=' || std::isspace(static_cast<unsigned char>(ch)))
            continue;
        const int value = base64Value(static_cast<unsigned char>(ch));
        if (value < 0)
            continue; // invalid characters are skipped leniently
        // Unsigned arithmetic: the high bits simply fall off (left shift of
        // an overflowing int would be UB); the low `bits + 6` bits stay valid.
        accumulator = (accumulator << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            dst.push_back(static_cast<char>((accumulator >> bits) & 0xFF));
        }
    }
    return out;
}

ByteArray ByteArray::toHex(char separator) const {
    static const char digits[] = "0123456789abcdef";
    ByteArray out;
    std::string& dst = out.d->data;
    for (std::size_t i = 0; i < d->data.size(); ++i) {
        if (separator != '\0' && i > 0)
            dst.push_back(separator);
        const auto byte = static_cast<unsigned char>(d->data[i]);
        dst.push_back(digits[byte >> 4]);
        dst.push_back(digits[byte & 0xF]);
    }
    return out;
}

ByteArray ByteArray::fromHex(const ByteArray& encoded) {
    ByteArray out;
    std::string& dst = out.d->data;
    int high = -1;
    for (const char ch : encoded.d->data) {
        const int value = hexValue(static_cast<unsigned char>(ch));
        if (value < 0)
            continue; // separators and other noise are skipped
        if (high < 0)
            high = value;
        else {
            dst.push_back(static_cast<char>((high << 4) | value));
            high = -1;
        }
    }
    return out;
}

} // namespace Firmir

