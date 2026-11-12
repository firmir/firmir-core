#include <firmir/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <variant>

namespace Firmir {

struct JsonArray::Impl {
    std::vector<JsonValue> items;
};

struct JsonObject::Impl {
    std::map<std::string, JsonValue> members; // sorted keys
};

struct JsonValue::Impl {
    using Storage =
        std::variant<std::monostate /*Undefined*/, std::nullptr_t /*Null*/, bool,
                     double, Firmir::String, JsonArray, JsonObject>;
    Storage storage;
};

// ---------------------------------------------------------------- JsonValue

JsonValue::JsonValue() : d(std::make_shared<Impl>()) { d->storage = nullptr; }

JsonValue JsonValue::undefined() {
    JsonValue v;
    v.d->storage = std::monostate{};
    return v;
}

JsonValue::JsonValue(bool value) : JsonValue() { d->storage = value; }
JsonValue::JsonValue(int value) : JsonValue() {
    d->storage = static_cast<double>(value);
}
JsonValue::JsonValue(long long value) : JsonValue() {
    d->storage = static_cast<double>(value);
}
JsonValue::JsonValue(double value) : JsonValue() { d->storage = value; }
JsonValue::JsonValue(const char* value) : JsonValue() {
    d->storage = Firmir::String(value ? value : "");
}
JsonValue::JsonValue(const Firmir::String& value) : JsonValue() {
    d->storage = value;
}
JsonValue::JsonValue(const JsonArray& value) : JsonValue() { d->storage = value; }
JsonValue::JsonValue(const JsonObject& value) : JsonValue() {
    d->storage = value;
}

JsonValue::Type JsonValue::type() const {
    switch (d->storage.index()) {
    case 0:
        return Type::Undefined;
    case 1:
        return Type::Null;
    case 2:
        return Type::Bool;
    case 3:
        return Type::Double;
    case 4:
        return Type::String;
    case 5:
        return Type::Array;
    case 6:
        return Type::Object;
    }
    return Type::Undefined;
}

bool JsonValue::toBool(bool defaultValue) const {
    return isBool() ? std::get<bool>(d->storage) : defaultValue;
}

int JsonValue::toInt(int defaultValue) const {
    return isDouble() ? static_cast<int>(std::get<double>(d->storage))
                      : defaultValue;
}

double JsonValue::toDouble(double defaultValue) const {
    return isDouble() ? std::get<double>(d->storage) : defaultValue;
}

Firmir::String JsonValue::toString(const Firmir::String& defaultValue) const {
    return isString() ? std::get<Firmir::String>(d->storage) : defaultValue;
}

JsonArray JsonValue::toArray() const {
    return isArray() ? std::get<JsonArray>(d->storage) : JsonArray();
}

JsonObject JsonValue::toObject() const {
    return isObject() ? std::get<JsonObject>(d->storage) : JsonObject();
}
// ---------------------------------------------------------------- JsonArray

JsonArray::JsonArray() : d(std::make_shared<Impl>()) {}

void JsonArray::detach() {
    // shared_ptr::unique() is deprecated in C++17; use_count() == 1 is the
    // documented equivalent for detach-on-write here (no weak refs exist).
    if (d.use_count() > 1)
        d = std::make_shared<Impl>(*d);
}

int JsonArray::size() const { return static_cast<int>(d->items.size()); }

JsonValue JsonArray::at(int i) const {
    if (i < 0 || i >= size())
        return JsonValue::undefined();
    return d->items[static_cast<std::size_t>(i)];
}

void JsonArray::append(const JsonValue& value) {
    detach();
    d->items.push_back(value);
}

void JsonArray::prepend(const JsonValue& value) { insert(0, value); }

void JsonArray::insert(int i, const JsonValue& value) {
    if (i < 0 || i > size())
        return;
    detach();
    d->items.insert(d->items.begin() + i, value);
}

void JsonArray::removeAt(int i) {
    if (i < 0 || i >= size())
        return;
    detach();
    d->items.erase(d->items.begin() + i);
}

// --------------------------------------------------------------- JsonObject

JsonObject::JsonObject() : d(std::make_shared<Impl>()) {}

void JsonObject::detach() {
    if (d.use_count() > 1)
        d = std::make_shared<Impl>(*d);
}

int JsonObject::size() const { return static_cast<int>(d->members.size()); }

bool JsonObject::contains(const Firmir::String& key) const {
    return d->members.count(key.toStdString()) != 0;
}

JsonValue JsonObject::value(const Firmir::String& key) const {
    const auto it = d->members.find(key.toStdString());
    return it == d->members.end() ? JsonValue::undefined() : it->second;
}

void JsonObject::insert(const Firmir::String& key, const JsonValue& value) {
    detach();
    d->members[key.toStdString()] = value;
}

void JsonObject::remove(const Firmir::String& key) {
    detach();
    d->members.erase(key.toStdString());
}

std::vector<Firmir::String> JsonObject::keys() const {
    std::vector<Firmir::String> out;
    out.reserve(d->members.size());
    for (const auto& [key, unused] : d->members)
        out.emplace_back(key);
    return out;
}

// ---------------------------------------------------------------- equality

bool operator==(const JsonArray& a, const JsonArray& b) {
    if (a.d == b.d)
        return true;
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (a.at(i) != b.at(i))
            return false;
    return true;
}

bool operator==(const JsonObject& a, const JsonObject& b) {
    if (a.d == b.d)
        return true;
    if (a.size() != b.size())
        return false;
    for (const auto& key : a.keys())
        if (a.value(key) != b.value(key))
            return false;
    return true;
}

bool operator==(const JsonValue& a, const JsonValue& b) {
    if (a.type() != b.type())
        return false;
    switch (a.type()) {
    case JsonValue::Type::Undefined:
    case JsonValue::Type::Null:
        return true;
    case JsonValue::Type::Bool:
        return a.toBool() == b.toBool();
    case JsonValue::Type::Double:
        return a.toDouble() == b.toDouble();
    case JsonValue::Type::String:
        return a.toString() == b.toString();
    case JsonValue::Type::Array:
        return a.toArray() == b.toArray();
    case JsonValue::Type::Object:
        return a.toObject() == b.toObject();
    }
    return false;
}


// ------------------------------------------------------------------ parser

namespace {

class JsonParser {
public:
    explicit JsonParser(std::string text) : t_(std::move(text)) {}

    bool parse(JsonValue& out) {
        skipWs();
        if (!parseValue(out))
            return false;
        skipWs();
        if (pos_ != t_.size()) {
            fail();
            return false;
        }
        return true;
    }

    int errorOffset() const { return errorOffset_; }

private:
    void fail() {
        if (errorOffset_ < 0)
            errorOffset_ = static_cast<int>(pos_);
    }

    void skipWs() {
        while (pos_ < t_.size() &&
               (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' ||
                t_[pos_] == '\r'))
            ++pos_;
    }

    bool consume(char c) {
        if (pos_ < t_.size() && t_[pos_] == c) {
            ++pos_;
            return true;
        }
        fail();
        return false;
    }

    bool literal(const char* word) {
        const std::size_t len = std::string(word).size();
        if (t_.compare(pos_, len, word) == 0) {
            pos_ += len;
            return true;
        }
        fail();
        return false;
    }

    bool parseValue(JsonValue& out) {
        if (pos_ >= t_.size()) {
            fail();
            return false;
        }
        switch (t_[pos_]) {
        case 'n':
            if (!literal("null"))
                return false;
            out = JsonValue();
            return true;
        case 't':
            if (!literal("true"))
                return false;
            out = JsonValue(true);
            return true;
        case 'f':
            if (!literal("false"))
                return false;
            out = JsonValue(false);
            return true;
        case '"': {
            Firmir::String s;
            if (!parseString(s))
                return false;
            out = JsonValue(s);
            return true;
        }
        case '[':
            return parseArray(out);
        case '{':
            return parseObject(out);
        default:
            return parseNumber(out);
        }
    }

    static void appendUtf8(std::string& out, unsigned int cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool parseHex4(unsigned int& out) {
        if (pos_ + 4 > t_.size()) {
            fail();
            return false;
        }
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<unsigned>(c - 'A' + 10);
            else {
                fail();
                return false;
            }
        }
        out = value;
        return true;
    }

    bool parseString(Firmir::String& out) {
        if (!consume('"'))
            return false;
        std::string result;
        while (pos_ < t_.size() && t_[pos_] != '"') {
            const char c = t_[pos_++];
            if (c != '\\') {
                result.push_back(c);
                continue;
            }
            if (pos_ >= t_.size()) {
                fail();
                return false;
            }
            const char esc = t_[pos_++];
            switch (esc) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                unsigned int cp = 0;
                if (!parseHex4(cp))
                    return false;
                if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < t_.size() &&
                    t_[pos_] == '\\' && t_[pos_ + 1] == 'u') {
                    pos_ += 2;
                    unsigned int low = 0;
                    if (!parseHex4(low))
                        return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                }
                appendUtf8(result, cp);
                break;
            }
            default:
                fail();
                return false;
            }
        }
        if (!consume('"'))
            return false;
        out = Firmir::String(result);
        return true;
    }

    bool parseNumber(JsonValue& out) {
        const std::size_t start = pos_;
        if (pos_ < t_.size() && t_[pos_] == '-')
            ++pos_;
        while (pos_ < t_.size() &&
               ((t_[pos_] >= '0' && t_[pos_] <= '9') || t_[pos_] == '.' ||
                t_[pos_] == 'e' || t_[pos_] == 'E' || t_[pos_] == '+' ||
                t_[pos_] == '-'))
            ++pos_;
        if (pos_ == start) {
            fail();
            return false;
        }
        out = JsonValue(
            std::strtod(t_.substr(start, pos_ - start).c_str(), nullptr));
        return true;
    }

    bool parseArray(JsonValue& out) {
        if (!consume('['))
            return false;
        JsonArray array;
        skipWs();
        if (pos_ < t_.size() && t_[pos_] == ']') {
            ++pos_;
            out = JsonValue(array);
            return true;
        }
        for (;;) {
            JsonValue element;
            skipWs();
            if (!parseValue(element))
                return false;
            array.append(element);
            skipWs();
            if (consume(']')) {
                out = JsonValue(array);
                return true;
            }
            if (!consume(','))
                return false;
        }
    }

    bool parseObject(JsonValue& out) {
        if (!consume('{'))
            return false;
        JsonObject object;
        skipWs();
        if (pos_ < t_.size() && t_[pos_] == '}') {
            ++pos_;
            out = JsonValue(object);
            return true;
        }
        for (;;) {
            skipWs();
            Firmir::String key;
            if (!parseString(key))
                return false;
            skipWs();
            if (!consume(':'))
                return false;
            skipWs();
            JsonValue value;
            if (!parseValue(value))
                return false;
            object.insert(key, value);
            skipWs();
            if (consume('}')) {
                out = JsonValue(object);
                return true;
            }
            if (!consume(','))
                return false;
        }
    }

    std::string t_;
    std::size_t pos_ = 0;
    int errorOffset_ = -1;
};

} // namespace



// --------------------------------------------------------------- serializer

namespace {

void appendEscaped(std::string& out, const std::string& s) {
    out.push_back('"');
    for (const unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

void appendJson(std::string& out, const JsonValue& value,
                JsonDocument::JsonFormat format, int indent) {
    const bool pretty = format == JsonDocument::JsonFormat::Indented;
    const std::string pad =
        pretty ? std::string(static_cast<std::size_t>(indent) * 4, ' ') : "";
    const std::string padInner =
        pretty ? std::string(static_cast<std::size_t>(indent + 1) * 4, ' ') : "";

    switch (value.type()) {
    case JsonValue::Type::Undefined:
    case JsonValue::Type::Null:
        out += "null";
        return;
    case JsonValue::Type::Bool:
        out += value.toBool() ? "true" : "false";
        return;
    case JsonValue::Type::String:
        appendEscaped(out, value.toString().toStdString());
        return;
    case JsonValue::Type::Double: {
        const double v = value.toDouble();
        char buf[40];
        if (v == std::floor(v) && std::fabs(v) < 9.0e15)
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        else
            std::snprintf(buf, sizeof(buf), "%.17g", v);
        out += buf;
        return;
    }
    case JsonValue::Type::Array: {
        const JsonArray array = value.toArray();
        out += '[';
        for (int i = 0; i < array.size(); ++i) {
            if (i > 0)
                out += ',';
            if (pretty)
                out += "\n" + padInner;
            appendJson(out, array.at(i), format, indent + 1);
        }
        if (pretty && !array.isEmpty())
            out += "\n" + pad;
        out += ']';
        return;
    }
    case JsonValue::Type::Object: {
        const JsonObject object = value.toObject();
        out += '{';
        bool first = true;
        for (const auto& key : object.keys()) {
            if (!first)
                out += ',';
            first = false;
            if (pretty)
                out += "\n" + padInner;
            appendEscaped(out, key.toStdString());
            out += ':';
            if (pretty)
                out += ' ';
            appendJson(out, object.value(key), format, indent + 1);
        }
        if (pretty && !object.isEmpty())
            out += "\n" + pad;
        out += '}';
        return;
    }
    }
}

} // namespace

// ------------------------------------------------------------- JsonDocument

struct JsonDocument::Impl {
    JsonValue root;
};

JsonDocument::JsonDocument() : d(std::make_unique<Impl>()) {}
JsonDocument::~JsonDocument() = default;
JsonDocument::JsonDocument(const JsonDocument& other)
    : d(std::make_unique<Impl>(*other.d)) {}
JsonDocument::JsonDocument(JsonDocument&&) noexcept = default;
JsonDocument& JsonDocument::operator=(const JsonDocument& other) {
    if (this != &other)
        d->root = other.d->root;
    return *this;
}
JsonDocument& JsonDocument::operator=(JsonDocument&&) noexcept = default;

JsonDocument JsonDocument::fromJson(const Firmir::ByteArray& json,
                                    ParseError* error) {
    JsonParser parser(json.toStdString());
    JsonDocument doc;
    JsonValue root;
    if (!parser.parse(root)) {
        if (error) {
            error->error = ParseError::SyntaxError;
            error->offset = parser.errorOffset();
        }
        return JsonDocument(); // null document
    }
    if (error) {
        error->error = ParseError::NoError;
        error->offset = 0;
    }
    doc.d->root = root;
    return doc;
}

Firmir::ByteArray JsonDocument::toJson(JsonFormat format) const {
    if (isNull())
        return Firmir::ByteArray();
    std::string out;
    appendJson(out, d->root, format, 0);
    if (format == JsonFormat::Indented)
        out += '\n';
    return Firmir::ByteArray(out);
}

bool JsonDocument::isNull() const {
    return d->root.isNull() || d->root.isUndefined();
}

bool JsonDocument::isArray() const { return d->root.isArray(); }
bool JsonDocument::isObject() const { return d->root.isObject(); }

JsonArray JsonDocument::array() const { return d->root.toArray(); }
JsonObject JsonDocument::object() const { return d->root.toObject(); }

void JsonDocument::setArray(const JsonArray& array) { d->root = JsonValue(array); }

void JsonDocument::setObject(const JsonObject& object) {
    d->root = JsonValue(object);
}

} // namespace Firmir
