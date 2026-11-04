#pragma once

#include <firmir/file.hpp>
#include <firmir/string.hpp>

#include <memory>

namespace Firmir {

/// Firmir analogue of QTextStream: formatted text I/O over a String
/// or a File. Reading is whitespace/line oriented like Qt.
class TextStream {
public:
    explicit TextStream(String* string); // reads/writes *string
    explicit TextStream(File* file);     // file must be open
    ~TextStream();                       // flushes pending writes

    TextStream(const TextStream&) = delete;
    TextStream& operator=(const TextStream&) = delete;

    TextStream& operator<<(const String& value);
    TextStream& operator<<(const char* value);
    TextStream& operator<<(char value);
    TextStream& operator<<(int value);
    TextStream& operator<<(long long value);
    TextStream& operator<<(double value);

    /// Reads one whitespace-delimited word.
    TextStream& operator>>(String& word);

    String readAll();
    String readLine();
    bool atEnd() const;
    void flush();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
