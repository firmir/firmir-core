#pragma once

#include <firmir/bytearray.hpp>
#include <firmir/string.hpp>

#include <cstdint>
#include <memory>

namespace Firmir {

/// Firmir analogue of QDataStream: endian-aware binary serialization
/// over a ByteArray buffer. Wire format:
/// big-endian numbers, length-prefixed strings (quint32 + bytes).
class DataStream {
public:
    enum class ByteOrder { BigEndian, LittleEndian };
    enum class Status { Ok, ReadPastEnd, WriteFailed };

    /// Write-only stream accumulating into an internal buffer.
    DataStream();
    /// Stream over an external buffer (read from current content, writes append).
    explicit DataStream(ByteArray* buffer);
    /// Read-only stream over existing data.
    explicit DataStream(const ByteArray& data);
    ~DataStream();

    DataStream(const DataStream&) = delete;
    DataStream& operator=(const DataStream&) = delete;

    void setByteOrder(ByteOrder order);
    ByteOrder byteOrder() const;

    Status status() const;
    void resetStatus();

    /// The accumulated buffer (for streams writing to internal storage).
    ByteArray data() const;

    DataStream& operator<<(std::int8_t value);
    DataStream& operator<<(std::uint8_t value);
    DataStream& operator<<(std::int32_t value);
    DataStream& operator<<(std::uint32_t value);
    DataStream& operator<<(std::int64_t value);
    DataStream& operator<<(std::uint64_t value);
    DataStream& operator<<(double value);
    DataStream& operator<<(bool value);
    DataStream& operator<<(const String& value);
    DataStream& operator<<(const ByteArray& value);

    DataStream& operator>>(std::int8_t& value);
    DataStream& operator>>(std::uint8_t& value);
    DataStream& operator>>(std::int32_t& value);
    DataStream& operator>>(std::uint32_t& value);
    DataStream& operator>>(std::int64_t& value);
    DataStream& operator>>(std::uint64_t& value);
    DataStream& operator>>(double& value);
    DataStream& operator>>(bool& value);
    DataStream& operator>>(String& value);
    DataStream& operator>>(ByteArray& value);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
