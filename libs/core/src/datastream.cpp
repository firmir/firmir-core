#include <firmir/datastream.hpp>

#include <cstring>

namespace Firmir {

struct DataStream::Impl {
    ByteArray owned;
    ByteArray* buf = &owned; // points to owned or an external buffer
    int pos = 0;
    DataStream::ByteOrder order = DataStream::ByteOrder::BigEndian;
    DataStream::Status status = DataStream::Status::Ok;

    void writeRaw(const void* data, int size) {
        if (size <= 0)
            return;
        buf->append(static_cast<const char*>(data), size);
    }

    bool readRaw(void* out, int size) {
        if (status != DataStream::Status::Ok)
            return false;
        if (size < 0 || pos + size > buf->size()) {
            status = DataStream::Status::ReadPastEnd;
            return false;
        }
        std::memcpy(out, buf->constData() + pos, static_cast<std::size_t>(size));
        pos += size;
        return true;
    }

    void writeUint64(std::uint64_t value, int bytes) {
        char tmp[8];
        if (order == DataStream::ByteOrder::BigEndian) {
            for (int i = 0; i < bytes; ++i)
                tmp[i] =
                    static_cast<char>((value >> (8 * (bytes - 1 - i))) & 0xFF);
        } else {
            for (int i = 0; i < bytes; ++i)
                tmp[i] = static_cast<char>((value >> (8 * i)) & 0xFF);
        }
        writeRaw(tmp, bytes);
    }

    std::uint64_t readUint64(int bytes) {
        char tmp[8] = {};
        if (!readRaw(tmp, bytes))
            return 0;
        std::uint64_t value = 0;
        if (order == DataStream::ByteOrder::BigEndian) {
            for (int i = 0; i < bytes; ++i)
                value = (value << 8) | static_cast<unsigned char>(tmp[i]);
        } else {
            for (int i = bytes - 1; i >= 0; --i)
                value = (value << 8) | static_cast<unsigned char>(tmp[i]);
        }
        return value;
    }
};

DataStream::DataStream() : d(std::make_unique<Impl>()) {}

DataStream::DataStream(ByteArray* buffer) : d(std::make_unique<Impl>()) {
    if (buffer)
        d->buf = buffer;
}

DataStream::DataStream(const ByteArray& data) : d(std::make_unique<Impl>()) {
    d->owned = data;
}

DataStream::~DataStream() = default;

void DataStream::setByteOrder(ByteOrder order) { d->order = order; }
DataStream::ByteOrder DataStream::byteOrder() const { return d->order; }

DataStream::Status DataStream::status() const { return d->status; }
void DataStream::resetStatus() { d->status = Status::Ok; }

ByteArray DataStream::data() const { return *d->buf; }

DataStream& DataStream::operator<<(std::int8_t value) {
    return *this << static_cast<std::uint8_t>(value);
}

DataStream& DataStream::operator<<(std::uint8_t value) {
   d->writeUint64(value, 1);
    return *this;
}

DataStream& DataStream::operator<<(std::int32_t value) {
    return *this << static_cast<std::uint32_t>(value);
}

DataStream& DataStream::operator<<(std::uint32_t value) {
   d->writeUint64(value, 4);
    return *this;
}

DataStream& DataStream::operator<<(std::int64_t value) {
    return *this << static_cast<std::uint64_t>(value);
}

DataStream& DataStream::operator<<(std::uint64_t value) {
   d->writeUint64(value, 8);
    return *this;
}

DataStream& DataStream::operator<<(double value) {
    std::uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
   d->writeUint64(bits, 8);
    return *this;
}

DataStream& DataStream::operator<<(bool value) {
   d->writeUint64(value ? 1 : 0, 1);
    return *this;
}

DataStream& DataStream::operator<<(const String& value) {
    const std::string s = value.toStdString();
    *this << static_cast<std::uint32_t>(s.size());
   d->writeRaw(s.data(), static_cast<int>(s.size()));
    return *this;
}

DataStream& DataStream::operator<<(const ByteArray& value) {
    *this << static_cast<std::uint32_t>(value.size());
   d->writeRaw(value.constData(), value.size());
    return *this;
}

DataStream& DataStream::operator>>(std::int8_t& value) {
    std::uint8_t v = 0;
    *this >> v;
    value = static_cast<std::int8_t>(v);
    return *this;
}

DataStream& DataStream::operator>>(std::uint8_t& value) {
    value = static_cast<std::uint8_t>(d->readUint64(1));
    return *this;
}

DataStream& DataStream::operator>>(std::int32_t& value) {
    std::uint32_t v = 0;
    *this >> v;
    value = static_cast<std::int32_t>(v);
    return *this;
}

DataStream& DataStream::operator>>(std::uint32_t& value) {
    value = static_cast<std::uint32_t>(d->readUint64(4));
    return *this;
}

DataStream& DataStream::operator>>(std::int64_t& value) {
    std::uint64_t v = 0;
    *this >> v;
    value = static_cast<std::int64_t>(v);
    return *this;
}

DataStream& DataStream::operator>>(std::uint64_t& value) {
    value = d->readUint64(8);
    return *this;
}

DataStream& DataStream::operator>>(double& value) {
    const std::uint64_t bits = d->readUint64(8);
    if (d->status == Status::Ok)
        std::memcpy(&value, &bits, sizeof(value));
    else
        value = 0.0;
    return *this;
}

DataStream& DataStream::operator>>(bool& value) {
    value = d->readUint64(1) != 0;
    return *this;
}

DataStream& DataStream::operator>>(String& value) {
    std::uint32_t length = 0;
    *this >> length;
    if (d->status != Status::Ok) {
        value = String();
        return *this;
    }
    std::string s(static_cast<std::size_t>(length), '\0');
    if (!d->readRaw(s.data(), static_cast<int>(length))) {
        value = String();
        return *this;
    }
    value = String(s);
    return *this;
}

DataStream& DataStream::operator>>(ByteArray& value) {
    std::uint32_t length = 0;
    *this >> length;
    if (d->status != Status::Ok) {
        value = ByteArray();
        return *this;
    }
    value.resize(static_cast<int>(length));
    if (!d->readRaw(value.data(), static_cast<int>(length))) {
        value = ByteArray();
        return *this;
    }
    return *this;
}

} // namespace Firmir

