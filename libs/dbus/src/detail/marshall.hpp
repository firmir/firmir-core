#pragma once

// Internal D-Bus wire marshalling (little-endian, D-Bus alignment rules).
// Not installed.

#include <firmir/dbusvalue.hpp>

#include <cstddef>
#include <string>

namespace Firmir {
namespace detail {

/// Alignment of the D-Bus type starting with signature character @p c.
std::size_t alignmentOf(char c);

/// End offset (one past) of the single complete signature starting at
/// @p pos inside @p signature; std::string::npos when malformed.
std::size_t signatureEnd(const std::string& signature, std::size_t pos);

class DBusMarshaller {
public:
    explicit DBusMarshaller(std::string& buffer) : buffer_(buffer) {}

    /// Appends @p value, padding to its alignment first.
    void writeValue(const DBusValue& value);
    /// Appends padding so the buffer size becomes a multiple of @p alignment.
    void pad(std::size_t alignment);

private:
    void writeRaw(const void* data, std::size_t size);
    template <typename T> void writePod(T value);
    void writeString(const std::string& text);        // 's'/'o' layout
    void writeArrayData(const std::string& elementSignature,
                        const std::vector<DBusValue>& values);
    void writeDictData(const DBusValue::Dict& dict);

    std::string& buffer_;
};

class DBusUnmarshaller {
public:
    DBusUnmarshaller(const char* data, std::size_t size)
        : data_(data), size_(size) {}

    /// Reads the value of the type described by the signature starting at
    /// @p sigPos (advanced past the consumed signature). False on malformed
    /// input; the output value is then indeterminate.
    bool readValue(const std::string& signature, std::size_t& sigPos,
                   DBusValue& out);

    std::size_t position() const { return pos_; }
    void setPosition(std::size_t pos) { pos_ = pos; }
    bool pad(std::size_t alignment);
    std::size_t size() const { return size_; }

private:
    bool readRaw(void* out, std::size_t size);
    template <typename T> bool readPod(T& out);
    bool readString(Firmir::String& out); // 's'/'o' layout

    const char* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

} // namespace detail
} // namespace Firmir
