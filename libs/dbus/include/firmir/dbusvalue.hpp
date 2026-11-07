#pragma once

// Firmir::DBusValue — a D-Bus value with its type signature, the analogue of
// a QDBusArgument element. Supports the full basic type set plus arrays,
// structs, dicts and variants:
//
//   DBusValue v = std::int32_t(42);              // signature "i"
//   DBusValue a = DBusValue::makeArray("s", {DBusValue(String("x"))});
//   DBusValue s = DBusValue::makeStruct({...});  // signature "(...)"
//   DBusValue d = DBusValue::makeDict("s", "i", {{key, val}});
//   DBusValue box = DBusValue::makeVariant(DBusValue(String("inner")));

#include <firmir/string.hpp>

#include <cstdint>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

namespace Firmir {

class DBusValue {
public:
    enum class Type {
        Invalid,
        Byte,    // 'y'
        Bool,    // 'b'
        Int16,   // 'n'
        UInt16,  // 'q'
        Int32,   // 'i'
        UInt32,  // 'u'
        Int64,   // 'x'
        UInt64,  // 't'
        Double,  // 'd'
        String,  // 's'
        ObjectPath, // 'o'
        Signature,  // 'g'
        Array,   // 'a...'
        Struct,  // '(...)'
        Dict,    // 'a{kv}'
        Variant  // 'v'
    };

    struct ObjectPathTag {
        Firmir::String value;
    };
    struct SignatureTag {
        Firmir::String value;
    };
    struct Array {
        std::string elementSignature;
        std::vector<DBusValue> values;
    };
    struct Struct {
        std::vector<DBusValue> values;
    };
    struct Dict {
        std::string keySignature;
        std::string valueSignature;
        std::vector<std::pair<DBusValue, DBusValue>> entries;
    };
    struct VariantBox {
        std::string signature;
        std::shared_ptr<DBusValue> value;
    };

    DBusValue(); // Invalid
    ~DBusValue();
    DBusValue(const DBusValue& other);
    DBusValue(DBusValue&& other) noexcept;
    DBusValue& operator=(const DBusValue& other);
    DBusValue& operator=(DBusValue&& other) noexcept;

    DBusValue(std::uint8_t value);              // NOLINT: implicit
    DBusValue(bool value);                      // NOLINT: implicit
    DBusValue(std::int16_t value);              // NOLINT: implicit
    DBusValue(std::uint16_t value);             // NOLINT: implicit
    DBusValue(std::int32_t value);              // NOLINT: implicit
    DBusValue(std::uint32_t value);             // NOLINT: implicit
    DBusValue(std::int64_t value);              // NOLINT: implicit
    DBusValue(std::uint64_t value);             // NOLINT: implicit
    DBusValue(double value);                    // NOLINT: implicit
    DBusValue(const Firmir::String& value);     // NOLINT: implicit ('s')
    DBusValue(const char* value);               // NOLINT: implicit ('s')

    static DBusValue objectPath(const Firmir::String& path);
    static DBusValue signature(const Firmir::String& signature);
    static DBusValue makeArray(std::string elementSignature,
                               std::vector<DBusValue> values);
    static DBusValue makeStruct(std::vector<DBusValue> values);
    static DBusValue makeDict(std::string keySignature,
                              std::string valueSignature,
                              std::vector<std::pair<DBusValue, DBusValue>> entries);
    static DBusValue makeVariant(DBusValue value);

    Type type() const;
    bool isValid() const { return type() != Type::Invalid; }

    /// The D-Bus type signature of this value ("" when invalid).
    std::string signatureString() const;

    bool toBool(bool defaultValue = false) const;
    std::uint8_t toUInt8(std::uint8_t defaultValue = 0) const;
    std::int16_t toInt16(std::int16_t defaultValue = 0) const;
    std::uint16_t toUInt16(std::uint16_t defaultValue = 0) const;
    std::int32_t toInt32(std::int32_t defaultValue = 0) const;
    std::uint32_t toUInt32(std::uint32_t defaultValue = 0) const;
    std::int64_t toInt64(std::int64_t defaultValue = 0) const;
    std::uint64_t toUInt64(std::uint64_t defaultValue = 0) const;
    double toDouble(double defaultValue = 0.0) const;
    Firmir::String toString() const; // for String/ObjectPath/Signature
    const Array* asArray() const;
    const Struct* asStruct() const;
    const Dict* asDict() const;
    const VariantBox* asVariant() const;

    bool operator==(const DBusValue& other) const;
    bool operator!=(const DBusValue& other) const { return !(*this == other); }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

inline bool operator==(const DBusValue::ObjectPathTag& a,
                       const DBusValue::ObjectPathTag& b) {
    return a.value == b.value;
}
inline bool operator==(const DBusValue::SignatureTag& a,
                       const DBusValue::SignatureTag& b) {
    return a.value == b.value;
}
inline bool operator==(const DBusValue::Array& a, const DBusValue::Array& b) {
    return a.elementSignature == b.elementSignature && a.values == b.values;
}
inline bool operator==(const DBusValue::Struct& a, const DBusValue::Struct& b) {
    return a.values == b.values;
}
inline bool operator==(const DBusValue::Dict& a, const DBusValue::Dict& b) {
    return a.keySignature == b.keySignature &&
           a.valueSignature == b.valueSignature && a.entries == b.entries;
}
inline bool operator==(const DBusValue::VariantBox& a,
                       const DBusValue::VariantBox& b) {
    return a.signature == b.signature &&
           ((!a.value && !b.value) ||
            (a.value && b.value && *a.value == *b.value));
}

} // namespace Firmir
