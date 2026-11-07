#pragma once

#include <firmir/bytearray.hpp>
#include <firmir/string.hpp>

#include <memory>
#include <vector>

namespace Firmir {

class JsonArray;
class JsonObject;

/// Firmir analogue of QJsonValue. Copies share data until a mutating
/// operation detaches (implicit sharing).
class JsonValue {
public:
    enum class Type { Null, Bool, Double, String, Array, Object, Undefined };

    JsonValue(); // Null
    static JsonValue undefined();

    JsonValue(bool value);                    // NOLINT: implicit
    JsonValue(int value);                     // NOLINT: implicit
    JsonValue(long long value);               // NOLINT: implicit
    JsonValue(double value);                  // NOLINT: implicit
    JsonValue(const char* value);             // NOLINT: implicit
    JsonValue(const Firmir::String& value);   // NOLINT: implicit
    JsonValue(const JsonArray& value);        // NOLINT: implicit
    JsonValue(const JsonObject& value);       // NOLINT: implicit

    Type type() const;
    bool isNull() const { return type() == Type::Null; }
    bool isUndefined() const { return type() == Type::Undefined; }
    bool isBool() const { return type() == Type::Bool; }
    bool isDouble() const { return type() == Type::Double; }
    bool isString() const { return type() == Type::String; }
    bool isArray() const { return type() == Type::Array; }
    bool isObject() const { return type() == Type::Object; }

    bool toBool(bool defaultValue = false) const;
    int toInt(int defaultValue = 0) const;
    double toDouble(double defaultValue = 0.0) const;
    Firmir::String toString(const Firmir::String& defaultValue = {}) const;
    JsonArray toArray() const;
    JsonObject toObject() const;

    friend bool operator==(const JsonValue& a, const JsonValue& b);
    friend bool operator!=(const JsonValue& a, const JsonValue& b) {
        return !(a == b);
    }

private:
    struct Impl;
    std::shared_ptr<Impl> d;
    friend bool operator==(const JsonValue& a, const JsonValue& b);
};

/// Firmir analogue of QJsonArray (ordered list of values).
class JsonArray {
public:
    JsonArray();

    int size() const;
    bool isEmpty() const { return size() == 0; }

    JsonValue at(int i) const;
    JsonValue operator[](int i) const { return at(i); }
    JsonValue first() const { return at(0); }
    JsonValue last() const { return at(size() - 1); }

    void append(const JsonValue& value);
    void push_back(const JsonValue& value) { append(value); }
    void prepend(const JsonValue& value);
    void insert(int i, const JsonValue& value);
    void removeAt(int i);

    friend bool operator==(const JsonArray& a, const JsonArray& b);
    friend bool operator!=(const JsonArray& a, const JsonArray& b) {
        return !(a == b);
    }

private:
    struct Impl;
    std::shared_ptr<Impl> d;
    void detach();
    friend class JsonDocument;
    friend struct JsonValueAccess;
};

/// Firmir analogue of QJsonObject (key -> value map; keys sorted).
class JsonObject {
public:
    JsonObject();

    int size() const;
    bool isEmpty() const { return size() == 0; }

    bool contains(const Firmir::String& key) const;
    JsonValue value(const Firmir::String& key) const; // Undefined if absent
    JsonValue operator[](const Firmir::String& key) const { return value(key); }

    void insert(const Firmir::String& key, const JsonValue& value);
    void remove(const Firmir::String& key);
    std::vector<Firmir::String> keys() const;

    friend bool operator==(const JsonObject& a, const JsonObject& b);
    friend bool operator!=(const JsonObject& a, const JsonObject& b) {
        return !(a == b);
    }

private:
    struct Impl;
    std::shared_ptr<Impl> d;
    void detach();
    friend class JsonDocument;
    friend struct JsonValueAccess;
};

/// Firmir analogue of QJsonDocument: owns a whole JSON document and
/// provides parsing/serialization.
class JsonDocument {
public:
    JsonDocument();
    ~JsonDocument();
    JsonDocument(const JsonDocument& other);
    JsonDocument(JsonDocument&& other) noexcept;
    JsonDocument& operator=(const JsonDocument& other);
    JsonDocument& operator=(JsonDocument&& other) noexcept;

    enum class JsonFormat { Indented, Compact };

    struct ParseError {
        enum Code { NoError, SyntaxError } error = NoError;
        int offset = 0;
        Firmir::String errorString() const {
            return error == NoError ? "no error occurred"
                                    : Firmir::String("syntax error");
        }
    };

    static JsonDocument fromJson(const Firmir::ByteArray& json,
                                 ParseError* error = nullptr);
    Firmir::ByteArray toJson(JsonFormat format = JsonFormat::Indented) const;

    bool isNull() const;
    bool isArray() const;
    bool isObject() const;

    JsonArray array() const;
    JsonObject object() const;
    void setArray(const JsonArray& array);
    void setObject(const JsonObject& object);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
