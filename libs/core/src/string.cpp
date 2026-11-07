#include <firmir/string.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace Firmir {

namespace {

std::string toLowerCopy(const std::string& s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

} // namespace

struct String::Impl {
    std::string data;
};

String::String() : d(std::make_unique<Impl>()) {}
String::~String() = default;
String::String(const String& other) : d(std::make_unique<Impl>(*other.d)) {}
String::String(String&& other) noexcept = default;
String& String::operator=(const String& other) {
    if (this != &other)
        d->data = other.d->data;
    return *this;
}
String& String::operator=(String&& other) noexcept = default;

String::String(const char* str) : d(std::make_unique<Impl>()) {
    d->data = str ? str : "";
}
String::String(const std::string& str) : d(std::make_unique<Impl>()) {
    d->data = str;
}
String::String(std::string&& str) : d(std::make_unique<Impl>()) {
    d->data = std::move(str);
}
String::String(int size, char ch) : d(std::make_unique<Impl>()) {
    d->data.assign(static_cast<std::size_t>(std::max(0, size)), ch);
}

int String::size() const { return static_cast<int>(d->data.size()); }
bool String::isEmpty() const { return d->data.empty(); }
void String::clear() { d->data.clear(); }

char String::at(int i) const { return d->data.at(static_cast<std::size_t>(i)); }

const char* String::c_str() const { return d->data.c_str(); }
std::string String::toStdString() const { return d->data; }

bool operator==(const String& a, const String& b) {
    return a.d->data == b.d->data;
}
bool operator<(const String& a, const String& b) {
    return a.d->data < b.d->data;
}
String operator+(const String& a, const String& b) {
    return String(a.d->data + b.d->data);
}
std::ostream& operator<<(std::ostream& os, const String& s) {
    return os << s.d->data;
}

String String::number(int value) { return String(std::to_string(value)); }

String String::number(long long value) { return String(std::to_string(value)); }

String String::number(double value, int precision) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*g", precision, value);
    return String(buf);
}

String& String::append(const String& other) {
    d->data += other.d->data;
    return *this;
}

String& String::prepend(const String& other) {
    d->data.insert(0, other.d->data);
    return *this;
}

String& String::operator+=(char ch) {
    d->data.push_back(ch);
    return *this;
}

int String::indexOf(const String& needle, int from, CaseSensitivity cs) const {
    if (from < 0 || needle.d->data.empty() ||
        static_cast<std::size_t>(from) >= d->data.size())
        return -1;
    const std::string& hay =
        cs == CaseSensitivity::Sensitive ? d->data : toLowerCopy(d->data);
    const std::string pat = cs == CaseSensitivity::Sensitive
                                ? needle.d->data
                                : toLowerCopy(needle.d->data);
    const std::size_t pos = hay.find(pat, static_cast<std::size_t>(from));
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

int String::lastIndexOf(const String& needle, int from, CaseSensitivity cs) const {
    if (needle.d->data.empty())
        return -1;
    const std::string& hay =
        cs == CaseSensitivity::Sensitive ? d->data : toLowerCopy(d->data);
    const std::string pat = cs == CaseSensitivity::Sensitive
                                ? needle.d->data
                                : toLowerCopy(needle.d->data);
    const std::size_t pos =
        from < 0 ? hay.rfind(pat) : hay.rfind(pat, static_cast<std::size_t>(from));
    return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

bool String::contains(const String& needle, CaseSensitivity cs) const {
    return indexOf(needle, 0, cs) >= 0;
}
bool String::startsWith(const String& prefix, CaseSensitivity cs) const {
    if (prefix.d->data.size() > d->data.size())
        return false;
    if (cs == CaseSensitivity::Sensitive)
        return d->data.compare(0, prefix.d->data.size(), prefix.d->data) == 0;
    return toLowerCopy(d->data.substr(0, prefix.d->data.size())) ==
           toLowerCopy(prefix.d->data);
}

bool String::endsWith(const String& suffix, CaseSensitivity cs) const {
    if (suffix.d->data.size() > d->data.size())
        return false;
    const std::size_t off = d->data.size() - suffix.d->data.size();
    if (cs == CaseSensitivity::Sensitive)
        return d->data.compare(off, suffix.d->data.size(), suffix.d->data) == 0;
    return toLowerCopy(d->data.substr(off)) == toLowerCopy(suffix.d->data);
}

String String::mid(int pos, int length) const {
    const int sz = size();
    if (pos < 0)
        pos = 0;
    if (pos >= sz)
        return String();
    if (length < 0 || pos + length > sz)
        length = sz - pos;
    return String(d->data.substr(static_cast<std::size_t>(pos),
                               static_cast<std::size_t>(length)));
}

String String::left(int n) const { return mid(0, std::max(0, n)); }

String String::right(int n) const {
    if (n <= 0)
        return String();
    return mid(std::max(0, size() - n));
}

String String::trimmed() const {
    std::size_t first = 0;
    std::size_t last = d->data.size();
    while (first < last && isSpace(d->data[first]))
        ++first;
    while (last > first && isSpace(d->data[last - 1]))
        --last;
    return String(d->data.substr(first, last - first));
}

String String::toLower() const { return String(toLowerCopy(d->data)); }

String String::toUpper() const {
    std::string out(d->data);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return String(std::move(out));
}

std::vector<String> String::split(char separator) const {
    std::vector<String> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i < d->data.size(); ++i) {
        if (d->data[i] == separator) {
            out.emplace_back(d->data.substr(start, i - start));
            start = i + 1;
        }
    }
    out.emplace_back(d->data.substr(start));
    return out;
}

std::vector<String> String::split(const String& separator) const {
    if (separator.d->data.empty())
        return {*this};
    std::vector<String> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t pos = d->data.find(separator.d->data, start);
        if (pos == std::string::npos)
            break;
        out.emplace_back(d->data.substr(start, pos - start));
        start = pos + separator.d->data.size();
    }
    out.emplace_back(d->data.substr(start));
    return out;
}

long long String::toLongLong(bool* ok, int base) const {
    const char* begin = d->data.c_str();
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(begin, &end, base);
    const bool good = end != begin && *end == '\0' && errno == 0;
    if (ok)
        *ok = good;
    return good ? value : 0;
}

int String::toInt(bool* ok, int base) const {
    return static_cast<int>(toLongLong(ok, base));
}

double String::toDouble(bool* ok) const {
    const char* begin = d->data.c_str();
    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(begin, &end);
    const bool good = end != begin && *end == '\0' && errno == 0;
    if (ok)
        *ok = good;
    return good ? value : 0.0;
}

String& String::replace(const String& before, const String& after) {
    if (before.d->data.empty())
        return *this;
    std::size_t pos = 0;
    while ((pos = d->data.find(before.d->data, pos)) != std::string::npos) {
        d->data.replace(pos, before.d->data.size(), after.d->data);
        pos += after.d->data.size();
    }
    return *this;
}

} // namespace Firmir

