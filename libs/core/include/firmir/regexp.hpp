#pragma once

// Firmir::RegExp — regular expression matching.
//
// The pattern syntax is ECMAScript (std::regex). A RegExp remembers the
// results of the last successful match attempt (cap()/pos()/matchedLength()),
// Matching semantics:
//
//   Firmir::RegExp re("(\\d+)-(\\d+)");
//   if (re.indexIn("port 8080-9090") != -1)
//       use(re.cap(1), re.cap(2)); // "8080", "9090"
//
// Note: a single instance is stateful (it stores the last
// match) and must not be shared between threads without synchronization.

#include <memory>
#include <string>
#include <vector>

namespace Firmir {

class RegExp {
public:
    enum class CaseSensitivity { Sensitive, Insensitive };

    RegExp();
    explicit RegExp(const std::string& pattern,
                    CaseSensitivity cs = CaseSensitivity::Sensitive);
    ~RegExp();

    RegExp(const RegExp& other);
    RegExp& operator=(const RegExp& other);
    RegExp(RegExp&& other) noexcept;
    RegExp& operator=(RegExp&& other) noexcept;

    /// False if the pattern failed to compile; see errorString().
    bool isValid() const;
    /// Description of the compilation error, empty when valid.
    std::string errorString() const;

    std::string pattern() const;
    void setPattern(const std::string& pattern);

    CaseSensitivity caseSensitivity() const;
    void setCaseSensitivity(CaseSensitivity cs);

    /// True when the pattern is empty.
    bool isEmpty() const;

    /// Non-greedy matching (setMinimal): every unescaped *, +, ? and
    /// {n,m} quantifier is converted to its lazy form when the pattern is
    /// compiled.
    void setMinimal(bool minimal);
    bool isMinimal() const;

    /// True if the pattern matches the whole @p text exactly.
    bool exactMatch(const std::string& text);

    /// Searches @p text starting at @p offset; returns the index of the first
    /// character of the match, or -1. On success the match results are
    /// available through cap()/pos()/matchedLength().
    int indexIn(const std::string& text, int offset = 0);

    /// Returns the index of the last match starting at or before @p offset
    /// (-1 searches from the end of the text), or -1 if there is no match.
    int lastIndexIn(const std::string& text, int offset = -1);

    /// Length of the last match (cap(0).size()), or -1 if there was none.
    int matchedLength() const;

    /// The @p nth captured substring of the last match; cap(0) is the whole
    /// match. Empty string when the group did not participate or is absent.
    std::string cap(int nth = 0) const;
    /// All captured substrings, cap(0) first.
    std::vector<std::string> capturedTexts() const;
    /// Position of the @p nth capture in the searched text, or -1.
    int pos(int nth = 0) const;
    /// Number of capturing groups in the pattern.
    int captureCount() const;

    /// Returns @p text with every regex metacharacter escaped, so it matches
    /// itself literally.
    static std::string escape(const std::string& text);

    bool operator==(const RegExp& other) const;
    bool operator!=(const RegExp& other) const { return !(*this == other); }

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
