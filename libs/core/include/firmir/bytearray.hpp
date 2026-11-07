#pragma once

#include <memory>
#include <ostream>
#include <string>

namespace Firmir {

/// A mutable sequence of bytes with the
/// usual search/convert helpers (Base64, hex, number parsing).
class ByteArray {
public:
    ByteArray();
    ~ByteArray();
    ByteArray(const ByteArray& other);
    ByteArray(ByteArray&& other) noexcept;
    ByteArray& operator=(const ByteArray& other);
    ByteArray& operator=(ByteArray&& other) noexcept;

    ByteArray(const char* data, int size = -1);   // NOLINT: implicit
    ByteArray(const std::string& data);           // NOLINT: implicit
    ByteArray(int size, char ch);

    int size() const;
    bool isEmpty() const;
    void clear();
    void resize(int size);

    char at(int i) const;
    char operator[](int i) const { return at(i); }

    char* data();
    const char* data() const;
    const char* constData() const { return data(); }

    ByteArray& append(const char* data, int size);
    ByteArray& append(const ByteArray& other);
    ByteArray& prepend(const ByteArray& other);
    ByteArray& operator+=(const ByteArray& other) { return append(other); }
    ByteArray& operator+=(char ch);

    int indexOf(const ByteArray& needle, int from = 0) const;
    bool contains(const ByteArray& needle) const { return indexOf(needle) >= 0; }
    bool startsWith(const ByteArray& prefix) const;
    bool endsWith(const ByteArray& suffix) const;

    ByteArray mid(int pos, int length = -1) const;
    ByteArray left(int n) const;
    ByteArray right(int n) const;
    ByteArray trimmed() const;
    ByteArray& remove(int pos, int length);

    int toInt(bool* ok = nullptr, int base = 10) const;
    double toDouble(bool* ok = nullptr) const;

    ByteArray toBase64() const;
    static ByteArray fromBase64(const ByteArray& encoded);

    ByteArray toHex(char separator = '\0') const;
    static ByteArray fromHex(const ByteArray& encoded);

    std::string toStdString() const;

    friend bool operator==(const ByteArray& a, const ByteArray& b);
    friend bool operator!=(const ByteArray& a, const ByteArray& b) {
        return !(a == b);
    }
    friend bool operator<(const ByteArray& a, const ByteArray& b);
    friend ByteArray operator+(const ByteArray& a, const ByteArray& b) {
        ByteArray out(a);
        out.append(b);
        return out;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
