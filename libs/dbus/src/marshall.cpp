#include "detail/marshall.hpp"

#include <cstring>

namespace Firmir {
namespace detail {

std::size_t alignmentOf(char c) {
    switch (c) {
    case 'y':
    case 'g':
    case 'v':
        return 1;
    case 'n':
    case 'q':
        return 2;
    case 'b':
    case 'i':
    case 'u':
    case 's':
    case 'o':
    case 'a':
        return 4;
    case 'x':
    case 't':
    case 'd':
    case '(':
    case '{':
        return 8;
    default:
        return 1;
    }
}

std::size_t signatureEnd(const std::string& signature, std::size_t pos) {
    if (pos >= signature.size())
        return std::string::npos;
    switch (signature[pos]) {
    case 'y':
    case 'b':
    case 'n':
    case 'q':
    case 'i':
    case 'u':
    case 'x':
    case 't':
    case 'd':
    case 's':
    case 'o':
    case 'g':
    case 'v':
    case 'h':
        return pos + 1;
    case 'a':
        return signatureEnd(signature, pos + 1);
    case '(': {
        std::size_t p = pos + 1;
        while (p < signature.size() && signature[p] != ')') {
            p = signatureEnd(signature, p);
            if (p == std::string::npos)
                return p;
        }
        return p < signature.size() ? p + 1 : std::string::npos;
    }
    case '{': {
        std::size_t p = pos + 1;
        while (p < signature.size() && signature[p] != '}') {
            p = signatureEnd(signature, p);
            if (p == std::string::npos)
                return p;
        }
        return p < signature.size() ? p + 1 : std::string::npos;
    }
    default:
        return std::string::npos;
    }
}

// ---------------------------------------------------------------------------
// Marshaller
// ---------------------------------------------------------------------------

void DBusMarshaller::pad(std::size_t alignment) {
    const std::size_t rest = buffer_.size() % alignment;
    if (rest)
        buffer_.append(alignment - rest, '\0');
}

void DBusMarshaller::writeRaw(const void* data, std::size_t size) {
    buffer_.append(static_cast<const char*>(data), size);
}

template <typename T> void DBusMarshaller::writePod(T value) {
    pad(alignof(T));
    writeRaw(&value, sizeof(value));
}

void DBusMarshaller::writeString(const std::string& text) {
    writePod<std::uint32_t>(static_cast<std::uint32_t>(text.size()));
    writeRaw(text.data(), text.size());
    writePod<std::uint8_t>(0);
}

void DBusMarshaller::writeArrayData(
    const std::string& elementSignature,
    const std::vector<DBusValue>& values) {
    pad(4);
    const std::size_t lengthPos = buffer_.size();
    writePod<std::uint32_t>(0); // patched below
    pad(alignmentOf(elementSignature.empty() ? 'y' : elementSignature[0]));
    const std::size_t start = buffer_.size();
    for (const DBusValue& value : values)
        writeValue(value);
    const std::uint32_t length =
        static_cast<std::uint32_t>(buffer_.size() - start);
    std::memcpy(buffer_.data() + lengthPos, &length, sizeof(length));
}

void DBusMarshaller::writeDictData(const DBusValue::Dict& dict) {
    pad(4);
    const std::size_t lengthPos = buffer_.size();
    writePod<std::uint32_t>(0); // patched below
    pad(8); // dict entries align to 8
    const std::size_t start = buffer_.size();
    for (const auto& [key, value] : dict.entries) {
        pad(8); // every dict entry is 8-aligned
        writeValue(key);
        writeValue(value);
    }
    const std::uint32_t length =
        static_cast<std::uint32_t>(buffer_.size() - start);
    std::memcpy(buffer_.data() + lengthPos, &length, sizeof(length));
}

void DBusMarshaller::writeValue(const DBusValue& value) {
    switch (value.type()) {
    case DBusValue::Type::Invalid:
        break;
    case DBusValue::Type::Byte:
        writePod<std::uint8_t>(value.toUInt8());
        break;
    case DBusValue::Type::Bool:
        writePod<std::uint32_t>(value.toBool() ? 1u : 0u);
        break;
    case DBusValue::Type::Int16:
        writePod<std::int16_t>(value.toInt16());
        break;
    case DBusValue::Type::UInt16:
        writePod<std::uint16_t>(value.toUInt16());
        break;
    case DBusValue::Type::Int32:
        writePod<std::int32_t>(value.toInt32());
        break;
    case DBusValue::Type::UInt32:
        writePod<std::uint32_t>(value.toUInt32());
        break;
    case DBusValue::Type::Int64:
        writePod<std::int64_t>(value.toInt64());
        break;
    case DBusValue::Type::UInt64:
        writePod<std::uint64_t>(value.toUInt64());
        break;
    case DBusValue::Type::Double:
        writePod<double>(value.toDouble());
        break;
    case DBusValue::Type::String:
    case DBusValue::Type::ObjectPath:
        writeString(value.toString().toStdString());
        break;
    case DBusValue::Type::Signature: {
        const std::string sig = value.toString().toStdString();
        writePod<std::uint8_t>(static_cast<std::uint8_t>(sig.size()));
        writeRaw(sig.data(), sig.size());
        writePod<std::uint8_t>(0);
        break;
    }
    case DBusValue::Type::Array: {
        const auto& array = *value.asArray();
        writeArrayData(array.elementSignature, array.values);
        break;
    }
    case DBusValue::Type::Dict:
        writeDictData(*value.asDict());
        break;
    case DBusValue::Type::Struct: {
        pad(8);
        for (const DBusValue& field : value.asStruct()->values)
            writeValue(field);
        break;
    }
    case DBusValue::Type::Variant: {
        const auto& box = *value.asVariant();
        writePod<std::uint8_t>(static_cast<std::uint8_t>(box.signature.size()));
        writeRaw(box.signature.data(), box.signature.size());
        writePod<std::uint8_t>(0);
        if (box.value)
            writeValue(*box.value);
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Unmarshaller
// ---------------------------------------------------------------------------

bool DBusUnmarshaller::pad(std::size_t alignment) {
    const std::size_t rest = pos_ % alignment;
    const std::size_t skip = rest ? alignment - rest : 0;
    if (pos_ + skip > size_)
        return false;
    pos_ += skip;
    return true;
}

bool DBusUnmarshaller::readRaw(void* out, std::size_t size) {
    if (pos_ + size > size_)
        return false;
    std::memcpy(out, data_ + pos_, size);
    pos_ += size;
    return true;
}

template <typename T> bool DBusUnmarshaller::readPod(T& out) {
    if (!pad(alignof(T)))
        return false;
    return readRaw(&out, sizeof(out));
}

bool DBusUnmarshaller::readString(Firmir::String& out) {
    std::uint32_t length = 0;
    if (!readPod(length))
        return false;
    if (pos_ + length + 1 > size_)
        return false;
    out = Firmir::String(std::string(data_ + pos_, length));
    pos_ += length;
    if (data_[pos_] != '\0')
        return false;
    ++pos_;
    return true;
}

bool DBusUnmarshaller::readValue(const std::string& signature,
                                 std::size_t& sigPos, DBusValue& out) {
    if (sigPos >= signature.size())
        return false;
    const char c = signature[sigPos++];
    switch (c) {
    case 'y': {
        std::uint8_t v;
        if (!readPod(v))
            return false;
        out = DBusValue(v);
        return true;
    }
    case 'b': {
        std::uint32_t v;
        if (!readPod(v))
            return false;
        out = DBusValue(v != 0);
        return true;
    }
    case 'n': {
        std::int16_t v;
        if (!readPod(v))
            return false;
        out = DBusValue(v);
        return true;
    }
    case 'q': {
        std::uint16_t v;
        if (!readPod(v))
            return false;
        out = DBusValue(v);
        return true;
    }
    case 'i': {
        std::int32_t v;
        if (!readPod(v))
            return false;
        out = DBusValue(v);
        return true;
    }
    case 'u': {
        std::uint32_t v;
        if (!readPod(v))
            return false;
        out = DBusValue(v);
        return true;
    }
    case 'x': {
        std::int64_t v;
        if (!readPod(v))
            return false;
        out = DBusValue(v);
        return true;
    }
    case 't': {
        std::uint64_t v;
        if (!readPod(v))
            return false;
        out = DBusValue(v);
        return true;
    }
    case 'd': {
        double v;
        if (!readPod(v))
            return false;
        out = DBusValue(v);
        return true;
    }
    case 's': {
        Firmir::String v;
        if (!readString(v))
            return false;
        out = DBusValue(v);
        return true;
    }
    case 'o': {
        Firmir::String v;
        if (!readString(v))
            return false;
        out = DBusValue::objectPath(v);
        return true;
    }
    case 'g': {
        std::uint8_t length = 0;
        if (!readPod(length))
            return false;
        if (pos_ + length + 1 > size_)
            return false;
        const Firmir::String v(std::string(data_ + pos_, length));
        pos_ += length;
        if (data_[pos_] != '\0')
            return false;
        ++pos_;
        out = DBusValue::signature(v);
        return true;
    }
    case 'v': {
        std::uint8_t length = 0;
        if (!readPod(length))
            return false;
        if (pos_ + length + 1 > size_)
            return false;
        const std::string inner(data_ + pos_, length);
        pos_ += length;
        if (data_[pos_] != '\0')
            return false;
        ++pos_;
        DBusValue innerValue;
        std::size_t innerPos = 0;
        if (!readValue(inner, innerPos, innerValue))
            return false;
        out = DBusValue::makeVariant(std::move(innerValue));
        return true;
    }
    case '(': {
        if (!pad(8))
            return false;
        std::vector<DBusValue> fields;
        while (sigPos < signature.size() && signature[sigPos] != ')') {
            DBusValue field;
            if (!readValue(signature, sigPos, field))
                return false;
            fields.push_back(std::move(field));
        }
        if (sigPos >= signature.size())
            return false;
        ++sigPos; // ')'
        out = DBusValue::makeStruct(std::move(fields));
        return true;
    }
    case 'a': {
        const std::size_t elemEnd = signatureEnd(signature, sigPos);
        if (elemEnd == std::string::npos)
            return false;
        const std::string elemSig = signature.substr(sigPos, elemEnd - sigPos);
        sigPos = elemEnd;

        std::uint32_t byteLength = 0;
        if (!readPod(byteLength))
            return false;
        if (elemSig.empty() || !pad(alignmentOf(elemSig[0])))
            return false;
        if (pos_ + byteLength > size_)
            return false;
        const std::size_t end = pos_ + byteLength;

        if (elemSig.size() >= 4 && elemSig[0] == '{' &&
            elemSig.back() == '}') {
            // Dict: an array of {key, value} entries.
            const std::string keySig = elemSig.substr(1, 1);
            const std::string valueSig =
                elemSig.substr(2, elemSig.size() - 3);
            std::vector<std::pair<DBusValue, DBusValue>> entries;
            while (pos_ < end) {
                if (!pad(8))
                    return false;
                DBusValue key, value;
                std::size_t keyPos = 0;
                if (!readValue(keySig, keyPos, key))
                    return false;
                std::size_t valuePos = 0;
                if (!readValue(valueSig, valuePos, value))
                    return false;
                entries.emplace_back(std::move(key), std::move(value));
            }
            if (pos_ != end)
                return false;
            out = DBusValue::makeDict(keySig, valueSig, std::move(entries));
            return true;
        }

        std::vector<DBusValue> values;
        while (pos_ < end) {
            DBusValue element;
            std::size_t innerSigPos = 0;
            if (!readValue(elemSig, innerSigPos, element))
                return false;
            values.push_back(std::move(element));
        }
        if (pos_ != end)
            return false;
        out = DBusValue::makeArray(elemSig, std::move(values));
        return true;
    }
    default:
        return false;
    }
}

} // namespace detail
} // namespace Firmir

