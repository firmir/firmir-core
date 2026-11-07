#include <firmir/regexp.hpp>

#include <algorithm>
#include <regex>
#include <utility>

namespace Firmir {

namespace {

/// Converts every greedy quantifier (*, +, ?, {n,m}) to its lazy form by
/// appending '?'. Escaped characters and characters inside a character class
/// are left alone.
std::string makeMinimal(const std::string& pattern) {
    std::string out;
    out.reserve(pattern.size() + 8);
    bool escaped = false;
    bool inClass = false;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        const char c = pattern[i];
        out.push_back(c);
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (inClass) {
            if (c == ']')
                inClass = false;
            continue;
        }
        if (c == '[') {
            inClass = true;
            continue;
        }
        bool quantifier = (c == '*' || c == '+' || c == '?');
        if (c == '{') {
            // {n} {n,} {n,m} count as quantifiers only if well-formed.
            const std::size_t close = pattern.find('}', i);
            if (close != std::string::npos) {
                bool wellFormed = close > i + 1;
                for (std::size_t j = i + 1; wellFormed && j < close; ++j)
                    wellFormed = (pattern[j] >= '0' && pattern[j] <= '9') ||
                                 pattern[j] == ',';
                if (wellFormed) {
                    quantifier = true;
                    out.append(pattern, i + 1, close - i); // copy up to '}'
                    i = close;
                }
            }
        }
        if (quantifier && (i + 1 >= pattern.size() || pattern[i + 1] != '?'))
            out.push_back('?');
    }
    return out;
}

} // namespace

struct RegExp::Impl {
    std::string pattern;
    CaseSensitivity cs = CaseSensitivity::Sensitive;
    bool minimal = false;
    bool valid = true;
    std::string error;

    std::regex compiled;

    // Results of the last successful match attempt.
    bool matched = false;
    std::vector<std::pair<int, int>> groups; // (position, length), -1 if unmatched
    std::vector<std::string> substrings;

    std::regex_constants::syntax_option_type flags() const {
        using SO = std::regex_constants::syntax_option_type;
        return std::regex_constants::ECMAScript |
               (cs == CaseSensitivity::Insensitive ? std::regex_constants::icase
                                                   : SO{});
    }

    void compile() {
        matched = false;
        groups.clear();
        substrings.clear();
        try {
            compiled =
                std::regex(minimal ? makeMinimal(pattern) : pattern, flags());
            valid = true;
            error.clear();
        } catch (const std::regex_error& e) {
            compiled = std::regex(); // empty regex, never matches
            valid = false;
            error = e.what();
        }
    }

    void storeMatch(const std::smatch& m, int baseOffset) {
        matched = true;
        groups.clear();
        substrings.clear();
        groups.reserve(m.size());
        substrings.reserve(m.size());
        for (int i = 0; i < static_cast<int>(m.size()); ++i) {
            const auto& sub = m[static_cast<std::size_t>(i)];
            if (sub.matched) {
                groups.emplace_back(static_cast<int>(m.position(i)) + baseOffset,
                                    static_cast<int>(m.length(i)));
                substrings.push_back(sub.str());
            } else {
                groups.emplace_back(-1, -1);
                substrings.emplace_back();
            }
        }
    }

    void clearMatch() {
        matched = false;
        groups.clear();
        substrings.clear();
    }
};

RegExp::RegExp() : d(std::make_unique<Impl>()) { d->compile(); }

RegExp::RegExp(const std::string& pattern, CaseSensitivity cs)
    : d(std::make_unique<Impl>()) {
    d->pattern = pattern;
    d->cs = cs;
    d->compile();
}

RegExp::~RegExp() = default;

RegExp::RegExp(const RegExp& other) : d(std::make_unique<Impl>(*other.d)) {}

RegExp& RegExp::operator=(const RegExp& other) {
    if (this != &other)
        *d = *other.d;
    return *this;
}

RegExp::RegExp(RegExp&&) noexcept = default;
RegExp& RegExp::operator=(RegExp&&) noexcept = default;

bool RegExp::isValid() const { return d->valid; }
std::string RegExp::errorString() const { return d->error; }

std::string RegExp::pattern() const { return d->pattern; }

void RegExp::setPattern(const std::string& pattern) {
    d->pattern = pattern;
    d->compile();
}

RegExp::CaseSensitivity RegExp::caseSensitivity() const { return d->cs; }

void RegExp::setCaseSensitivity(CaseSensitivity cs) {
    if (d->cs != cs) {
        d->cs = cs;
        d->compile();
    }
}

bool RegExp::isEmpty() const { return d->pattern.empty(); }

void RegExp::setMinimal(bool minimal) {
    if (d->minimal != minimal) {
        d->minimal = minimal;
        d->compile();
    }
}

bool RegExp::isMinimal() const { return d->minimal; }

bool RegExp::exactMatch(const std::string& text) {
    d->clearMatch();
    if (!d->valid)
        return false;
    std::smatch m;
    if (!std::regex_match(text, m, d->compiled))
        return false;
    d->storeMatch(m, 0);
    return true;
}

int RegExp::indexIn(const std::string& text, int offset) {
    d->clearMatch();
    if (!d->valid || offset < 0 ||
        static_cast<std::size_t>(offset) > text.size())
        return -1;
    std::smatch m;
    const auto begin = text.begin() + offset;
    if (!std::regex_search(begin, text.end(), m, d->compiled))
        return -1;
    d->storeMatch(m, offset);
    return d->groups[0].first;
}

int RegExp::lastIndexIn(const std::string& text, int offset) {
    d->clearMatch();
    if (!d->valid || text.empty())
        return -1;
    // A match must start at or before startLimit (offset -1 = last char).
    const int textEnd = static_cast<int>(text.size());
    const int startLimit =
        std::min(offset < 0 ? textEnd - 1 : offset, textEnd - 1);
    if (startLimit < 0)
        return -1;

    std::smatch last;
    bool found = false;
    using Iter = std::regex_iterator<std::string::const_iterator>;
    for (Iter it(text.begin(), text.end(), d->compiled); it != Iter(); ++it) {
        if (it->position(0) > startLimit)
            break;
        last = *it; // positions are relative to text.begin() == absolute
        found = true;
    }
    if (!found)
        return -1;
    d->storeMatch(last, 0);
    return d->groups[0].first;
}

int RegExp::matchedLength() const {
    if (!d->matched || d->groups.empty() || d->groups[0].second < 0)
        return -1;
    return d->groups[0].second;
}

std::string RegExp::cap(int nth) const {
    if (!d->matched || nth < 0 ||
        static_cast<std::size_t>(nth) >= d->substrings.size())
        return {};
    return d->substrings[static_cast<std::size_t>(nth)];
}

std::vector<std::string> RegExp::capturedTexts() const {
    return d->matched ? d->substrings : std::vector<std::string>();
}

int RegExp::pos(int nth) const {
    if (!d->matched || nth < 0 ||
        static_cast<std::size_t>(nth) >= d->groups.size())
        return -1;
    return d->groups[static_cast<std::size_t>(nth)].first;
}

int RegExp::captureCount() const {
    return d->valid ? static_cast<int>(d->compiled.mark_count()) : 0;
}

std::string RegExp::escape(const std::string& text) {
    static const std::string special = "\\^$.|?*+()[]{}";
    std::string out;
    out.reserve(text.size() * 2);
    for (const char c : text) {
        if (special.find(c) != std::string::npos)
            out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

bool RegExp::operator==(const RegExp& other) const {
    return d->pattern == other.d->pattern && d->cs == other.d->cs &&
           d->minimal == other.d->minimal;
}

} // namespace Firmir
