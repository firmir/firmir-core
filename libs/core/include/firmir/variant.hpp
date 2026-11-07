#pragma once

#include <firmir/bytearray.hpp>
#include <firmir/string.hpp>

#include <memory>

namespace Firmir {

/// A type-tagged union with lenient
/// lenient conversions between the supported types.
class Variant {
public:
    enum class Type { Invalid, Bool, Int, LongLong, Double, String, ByteArray };

    Variant(); // Invalid
    ~Variant();
    Variant(const Variant& other);
    Variant(Variant&& other) noexcept;
    Variant& operator=(const Variant& other);
    Variant& operator=(Variant&& other) noexcept;

    Variant(bool value);                    // NOLINT: implicit
    Variant(int value);                     // NOLINT: implicit
    Variant(long long value);               // NOLINT: implicit
    Variant(double value);                  // NOLINT: implicit
    Variant(const char* value);             // NOLINT: implicit
    Variant(const Firmir::String& value);   // NOLINT: implicit
    Variant(const Firmir::ByteArray& value); // NOLINT: implicit

    Type type() const;
    bool isValid() const { return type() != Type::Invalid; }
    bool isNull() const { return !isValid(); }

    bool toBool() const;
    int toInt(bool* ok = nullptr) const;
    long long toLongLong(bool* ok = nullptr) const;
    double toDouble(bool* ok = nullptr) const;
    Firmir::String toString() const;
    Firmir::ByteArray toByteArray() const;

    friend bool operator==(const Variant& a, const Variant& b);
    friend bool operator!=(const Variant& a, const Variant& b) {
        return !(a == b);
    }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
