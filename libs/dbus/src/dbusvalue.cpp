#include <firmir/dbusvalue.hpp>

namespace Firmir {

struct DBusValue::Impl {
    using Storage =
        std::variant<std::monostate, std::uint8_t, bool, std::int16_t,
                     std::uint16_t, std::int32_t, std::uint32_t, std::int64_t,
                     std::uint64_t, double, Firmir::String, ObjectPathTag,
                     SignatureTag, Array, Struct, Dict, VariantBox>;
    Storage storage;
};

DBusValue::DBusValue() : d(std::make_unique<Impl>()) {}
DBusValue::~DBusValue() = default;
DBusValue::DBusValue(const DBusValue& other)
    : d(std::make_unique<Impl>(*other.d)) {}
DBusValue::DBusValue(DBusValue&&) noexcept = default;
DBusValue& DBusValue::operator=(const DBusValue& other) {
    if (this != &other)
        d->storage = other.d->storage;
    return *this;
}
DBusValue& DBusValue::operator=(DBusValue&&) noexcept = default;

DBusValue::DBusValue(std::uint8_t value) : DBusValue() { d->storage = value; }
DBusValue::DBusValue(bool value) : DBusValue() { d->storage = value; }
DBusValue::DBusValue(std::int16_t value) : DBusValue() { d->storage = value; }
DBusValue::DBusValue(std::uint16_t value) : DBusValue() { d->storage = value; }
DBusValue::DBusValue(std::int32_t value) : DBusValue() { d->storage = value; }
DBusValue::DBusValue(std::uint32_t value) : DBusValue() { d->storage = value; }
DBusValue::DBusValue(std::int64_t value) : DBusValue() { d->storage = value; }
DBusValue::DBusValue(std::uint64_t value) : DBusValue() { d->storage = value; }
DBusValue::DBusValue(double value) : DBusValue() { d->storage = value; }
DBusValue::DBusValue(const Firmir::String& value) : DBusValue() {
    d->storage = value;
}
DBusValue::DBusValue(const char* value) : DBusValue() {
    d->storage = Firmir::String(value);
}

DBusValue DBusValue::objectPath(const Firmir::String& path) {
    DBusValue v;
    v.d->storage = ObjectPathTag{path};
    return v;
}

DBusValue DBusValue::signature(const Firmir::String& sig) {
    DBusValue v;
    v.d->storage = SignatureTag{sig};
    return v;
}

DBusValue DBusValue::makeArray(std::string elementSignature,
                               std::vector<DBusValue> values) {
    DBusValue v;
    v.d->storage = Array{std::move(elementSignature), std::move(values)};
    return v;
}

DBusValue DBusValue::makeStruct(std::vector<DBusValue> values) {
    DBusValue v;
    v.d->storage = Struct{std::move(values)};
    return v;
}

DBusValue DBusValue::makeDict(
    std::string keySignature, std::string valueSignature,
    std::vector<std::pair<DBusValue, DBusValue>> entries) {
    DBusValue v;
    v.d->storage = Dict{std::move(keySignature), std::move(valueSignature),
                      std::move(entries)};
    return v;
}

DBusValue DBusValue::makeVariant(DBusValue value) {
    DBusValue v;
    VariantBox box;
    box.signature = value.signatureString();
    box.value = std::make_shared<DBusValue>(std::move(value));
    v.d->storage = std::move(box);
    return v;
}

DBusValue::Type DBusValue::type() const {
    return static_cast<Type>(d->storage.index());
}

std::string DBusValue::signatureString() const {
    switch (type()) {
    case Type::Byte: return "y";
    case Type::Bool: return "b";
    case Type::Int16: return "n";
    case Type::UInt16: return "q";
    case Type::Int32: return "i";
    case Type::UInt32: return "u";
    case Type::Int64: return "x";
    case Type::UInt64: return "t";
    case Type::Double: return "d";
    case Type::String: return "s";
    case Type::ObjectPath: return "o";
    case Type::Signature: return "g";
    case Type::Variant: return "v";
    case Type::Array:
        return "a" + std::get<Array>(d->storage).elementSignature;
    case Type::Struct: {
        std::string sig = "(";
        for (const DBusValue& field : std::get<Struct>(d->storage).values)
            sig += field.signatureString();
        sig += ')';
        return sig;
    }
    case Type::Dict: {
        const Dict& dict = std::get<Dict>(d->storage);
        return "a{" + dict.keySignature + dict.valueSignature + "}";
    }
    case Type::Invalid:
        break;
    }
    return {};
}

bool DBusValue::toBool(bool defaultValue) const {
    if (const auto* v = std::get_if<bool>(&d->storage))
        return *v;
    return defaultValue;
}

std::uint8_t DBusValue::toUInt8(std::uint8_t defaultValue) const {
    if (const auto* v = std::get_if<std::uint8_t>(&d->storage))
        return *v;
    return defaultValue;
}

std::int16_t DBusValue::toInt16(std::int16_t defaultValue) const {
    if (const auto* v = std::get_if<std::int16_t>(&d->storage))
        return *v;
    return defaultValue;
}

std::uint16_t DBusValue::toUInt16(std::uint16_t defaultValue) const {
    if (const auto* v = std::get_if<std::uint16_t>(&d->storage))
        return *v;
    return defaultValue;
}

std::int32_t DBusValue::toInt32(std::int32_t defaultValue) const {
    if (const auto* v = std::get_if<std::int32_t>(&d->storage))
        return *v;
    return defaultValue;
}

std::uint32_t DBusValue::toUInt32(std::uint32_t defaultValue) const {
    if (const auto* v = std::get_if<std::uint32_t>(&d->storage))
        return *v;
    return defaultValue;
}

std::int64_t DBusValue::toInt64(std::int64_t defaultValue) const {
    if (const auto* v = std::get_if<std::int64_t>(&d->storage))
        return *v;
    return defaultValue;
}

std::uint64_t DBusValue::toUInt64(std::uint64_t defaultValue) const {
    if (const auto* v = std::get_if<std::uint64_t>(&d->storage))
        return *v;
    return defaultValue;
}

double DBusValue::toDouble(double defaultValue) const {
    if (const auto* v = std::get_if<double>(&d->storage))
        return *v;
    return defaultValue;
}

Firmir::String DBusValue::toString() const {
    if (const auto* v = std::get_if<Firmir::String>(&d->storage))
        return *v;
    if (const auto* v = std::get_if<ObjectPathTag>(&d->storage))
        return v->value;
    if (const auto* v = std::get_if<SignatureTag>(&d->storage))
        return v->value;
    return Firmir::String();
}

const DBusValue::Array* DBusValue::asArray() const {
    return std::get_if<Array>(&d->storage);
}

const DBusValue::Struct* DBusValue::asStruct() const {
    return std::get_if<Struct>(&d->storage);
}

const DBusValue::Dict* DBusValue::asDict() const {
    return std::get_if<Dict>(&d->storage);
}

const DBusValue::VariantBox* DBusValue::asVariant() const {
    return std::get_if<VariantBox>(&d->storage);
}

bool DBusValue::operator==(const DBusValue& other) const {
    return d->storage == other.d->storage;
}

} // namespace Firmir
