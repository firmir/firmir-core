#pragma once

#include <memory>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

namespace Firmir {

/// Firmir string class.
///
/// String stores UTF-8 bytes in a std::string, so
/// size()/at() operate on bytes, not Unicode code points. All comparisons and
/// searches support case sensitivity flags.
class String {
public:
    enum class CaseSensitivity { Sensitive, Insensitive };

    String();
    ~String();
    String(const String& other);
    String(String&& other) noexcept;
    String& operator=(const String& other);
    String& operator=(String&& other) noexcept;

    String(const char* str);                    // NOLINT: implicit
    String(const std::string& str);             // NOLINT: implicit
    String(std::string&& str);                  // NOLINT: implicit
    String(int size, char ch);

    static String number(int value);
    static String number(long long value);
    static String number(double value, int precision = 6);

    int size() const;
    int length() const { return size(); }
    bool isEmpty() const;
    void clear();

    /// Byte at \a i (UTF-8 semantics; no bounds check in release builds).
    char at(int i) const;
    char operator[](int i) const { return at(i); }

    const char* c_str() const;
    std::string toStdString() const;

    String& append(const String& other);
    String& prepend(const String& other);
    String& operator+=(const String& other) { return append(other); }
    String& operator+=(char ch);

    int indexOf(const String& needle, int from = 0,
                CaseSensitivity cs = CaseSensitivity::Sensitive) const;
    int lastIndexOf(const String& needle, int from = -1,
                    CaseSensitivity cs = CaseSensitivity::Sensitive) const;
    bool contains(const String& needle,
                  CaseSensitivity cs = CaseSensitivity::Sensitive) const;
    bool startsWith(const String& prefix,
                    CaseSensitivity cs = CaseSensitivity::Sensitive) const;
    bool endsWith(const String& suffix,
                  CaseSensitivity cs = CaseSensitivity::Sensitive) const;

    String mid(int pos, int length = -1) const;
    String left(int n) const;
    String right(int n) const;
    String trimmed() const;
    String toLower() const;
    String toUpper() const;

    std::vector<String> split(char separator) const;
    std::vector<String> split(const String& separator) const;

    int toInt(bool* ok = nullptr, int base = 10) const;
    long long toLongLong(bool* ok = nullptr, int base = 10) const;
    double toDouble(bool* ok = nullptr) const;

    String& replace(const String& before, const String& after);
    String& remove(const String& what) { return replace(what, String()); }

    /// arg()-style substitution: replaces the lowest-numbered
    /// %1..%9 marker with the stringified value.
    template <typename T> String arg(T&& value) const {
        std::ostringstream oss;
        oss << std::forward<T>(value);
        String result(*this);
        for (int n = 1; n <= 9; ++n) {
            const String marker = String("%") + String::number(n);
            if (result.contains(marker)) {
                result.replace(marker, String(oss.str()));
                break;
            }
        }
        return result;
    }

    friend bool operator==(const String& a, const String& b);
    friend bool operator!=(const String& a, const String& b) { return !(a == b); }
    friend bool operator<(const String& a, const String& b);
    friend String operator+(const String& a, const String& b);
    friend std::ostream& operator<<(std::ostream& os, const String& s);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
