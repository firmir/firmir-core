#include <firmir/variant.hpp>

#include <variant>

namespace Firmir {

struct Variant::Impl {
    using Storage = std::variant<std::monostate, bool, int, long long, double,
                                 Firmir::String, Firmir::ByteArray>;
    Storage value;
};

Variant::Variant() : d(std::make_unique<Impl>()) {}
Variant::~Variant() = default;
Variant::Variant(const Variant& other) : d(std::make_unique<Impl>(*other.d)) {}
Variant::Variant(Variant&& other) noexcept = default;
Variant& Variant::operator=(const Variant& other) {
    if (this != &other)
        d->value = other.d->value;
    return *this;
}
Variant& Variant::operator=(Variant&& other) noexcept = default;

Variant::Variant(bool value) : d(std::make_unique<Impl>()) { d->value = value; }
Variant::Variant(int value) : d(std::make_unique<Impl>()) { d->value = value; }
Variant::Variant(long long value) : d(std::make_unique<Impl>()) {
    d->value = value;
}
Variant::Variant(double value) : d(std::make_unique<Impl>()) { d->value = value; }
Variant::Variant(const char* value) : d(std::make_unique<Impl>()) {
    d->value = Firmir::String(value);
}
Variant::Variant(const Firmir::String& value) : d(std::make_unique<Impl>()) {
    d->value = value;
}
Variant::Variant(const Firmir::ByteArray& value) : d(std::make_unique<Impl>()) {
    d->value = value;
}

bool operator==(const Variant& a, const Variant& b) {
    return a.d->value == b.d->value;
}

Variant::Type Variant::type() const {
    switch (d->value.index()) {
    case 1:
        return Type::Bool;
    case 2:
        return Type::Int;
    case 3:
        return Type::LongLong;
    case 4:
        return Type::Double;
    case 5:
        return Type::String;
    case 6:
        return Type::ByteArray;
    default:
        return Type::Invalid;
    }
}

namespace {

void setOk(bool* ok, bool value) {
    if (ok)
        *ok = value;
}

} // namespace

bool Variant::toBool() const {
    switch (type()) {
    case Type::Bool:
        return std::get<bool>(d->value);
    case Type::Int:
        return std::get<int>(d->value) != 0;
    case Type::LongLong:
        return std::get<long long>(d->value) != 0;
    case Type::Double:
        return std::get<double>(d->value) != 0.0;
    case Type::String:
        return std::get<Firmir::String>(d->value).toLower() == "true" ||
               std::get<Firmir::String>(d->value).toInt() != 0;
    default:
        return false;
    }
}

long long Variant::toLongLong(bool* ok) const {
    switch (type()) {
    case Type::Bool:
        setOk(ok, true);
        return std::get<bool>(d->value) ? 1 : 0;
    case Type::Int:
        setOk(ok, true);
        return std::get<int>(d->value);
    case Type::LongLong:
        setOk(ok, true);
        return std::get<long long>(d->value);
    case Type::Double:
        setOk(ok, true);
        return static_cast<long long>(std::get<double>(d->value));
    case Type::String:
        return std::get<Firmir::String>(d->value).toLongLong(ok);
    case Type::ByteArray:
        return std::get<Firmir::ByteArray>(d->value).toInt(ok);
    default:
        setOk(ok, false);
        return 0;
    }
}

int Variant::toInt(bool* ok) const {
    bool good = false;
    const long long value = toLongLong(&good);
    setOk(ok, good);
    return static_cast<int>(value);
}

double Variant::toDouble(bool* ok) const {
    switch (type()) {
    case Type::Bool:
        setOk(ok, true);
        return std::get<bool>(d->value) ? 1.0 : 0.0;
    case Type::Int:
        setOk(ok, true);
        return std::get<int>(d->value);
    case Type::LongLong:
        setOk(ok, true);
        return static_cast<double>(std::get<long long>(d->value));
    case Type::Double:
        setOk(ok, true);
        return std::get<double>(d->value);
    case Type::String:
        return std::get<Firmir::String>(d->value).toDouble(ok);
    case Type::ByteArray:
        return std::get<Firmir::ByteArray>(d->value).toDouble(ok);
    default:
        setOk(ok, false);
        return 0.0;
    }
}

Firmir::String Variant::toString() const {
    switch (type()) {
    case Type::Bool:
        return std::get<bool>(d->value) ? "true" : "false";
    case Type::Int:
        return Firmir::String::number(std::get<int>(d->value));
    case Type::LongLong:
        return Firmir::String::number(std::get<long long>(d->value));
    case Type::Double:
        return Firmir::String::number(std::get<double>(d->value));
    case Type::String:
        return std::get<Firmir::String>(d->value);
    case Type::ByteArray:
        return Firmir::String(std::get<Firmir::ByteArray>(d->value).toStdString());
    default:
        return Firmir::String();
    }
}

Firmir::ByteArray Variant::toByteArray() const {
    switch (type()) {
    case Type::ByteArray:
        return std::get<Firmir::ByteArray>(d->value);
    case Type::String:
        return Firmir::ByteArray(std::get<Firmir::String>(d->value).toStdString());
    default:
        return Firmir::ByteArray(toString().toStdString());
    }
}

} // namespace Firmir
