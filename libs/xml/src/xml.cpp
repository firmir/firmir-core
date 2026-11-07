#include <firmir/xml.hpp>

#include <cctype>
#include <functional>
#include <utility>

namespace Firmir {

// ---------------------------------------------------------------------------
// Node
// ---------------------------------------------------------------------------

struct XmlElement::Node {
    String tag;
    std::vector<std::pair<String, String>> attributes; // insertion order
    std::vector<String> texts; // direct character data chunks, in order
    std::vector<std::shared_ptr<Node>> children;
};

XmlElement::XmlElement() = default;
XmlElement::XmlElement(std::shared_ptr<Node> node) : d(std::move(node)) {}

bool XmlElement::isNull() const { return !d; }
String XmlElement::tagName() const { return d ? d->tag : String(); }

String XmlElement::attribute(const String& name,
                             const String& defaultValue) const {
    if (!d)
        return defaultValue;
    for (const auto& attr : d->attributes)
        if (attr.first == name)
            return attr.second;
    return defaultValue;
}

bool XmlElement::hasAttribute(const String& name) const {
    if (!d)
        return false;
    for (const auto& attr : d->attributes)
        if (attr.first == name)
            return true;
    return false;
}

std::vector<String> XmlElement::attributeNames() const {
    std::vector<String> names;
    if (d)
        for (const auto& attr : d->attributes)
            names.push_back(attr.first);
    return names;
}

void XmlElement::setAttribute(const String& name, const String& value) {
    if (!d)
        return;
    for (auto& attr : d->attributes) {
        if (attr.first == name) {
            attr.second = value;
            return;
        }
    }
    d->attributes.emplace_back(name, value);
}

void XmlElement::removeAttribute(const String& name) {
    if (!d)
        return;
    for (auto it = d->attributes.begin(); it != d->attributes.end(); ++it) {
        if (it->first == name) {
            d->attributes.erase(it);
            return;
        }
    }
}

String XmlElement::text() const {
    if (!d)
        return String();
    String out;
    for (const String& chunk : d->texts)
        out += chunk;
    return out;
}

void XmlElement::setText(const String& text) {
    if (!d)
        return;
    d->texts.assign(1, text);
    d->children.clear();
}

std::vector<XmlElement> XmlElement::childElements() const {
    std::vector<XmlElement> out;
    if (d)
        for (const auto& child : d->children)
            out.push_back(XmlElement(child));
    return out;
}

std::vector<XmlElement> XmlElement::childElements(const String& tagName) const {
    std::vector<XmlElement> out;
    if (d)
        for (const auto& child : d->children)
            if (child->tag == tagName)
                out.push_back(XmlElement(child));
    return out;
}

XmlElement XmlElement::firstChildElement(const String& tagName) const {
    if (d)
        for (const auto& child : d->children)
            if (tagName.isEmpty() || child->tag == tagName)
                return XmlElement(child);
    return XmlElement();
}

std::vector<XmlElement>
XmlElement::elementsByTagName(const String& tagName) const {
    std::vector<XmlElement> out;
    if (!d)
        return out;
    const std::function<void(const std::shared_ptr<Node>&)> walk =
        [&](const std::shared_ptr<Node>& node) {
            for (const auto& child : node->children) {
                if (child->tag == tagName)
                    out.push_back(XmlElement(child));
                walk(child);
            }
        };
    walk(d);
    return out;
}

void XmlElement::appendChild(const XmlElement& child) {
    if (d && child.d)
        d->children.push_back(child.d);
}

void XmlElement::removeChild(const XmlElement& child) {
    if (!d || !child.d)
        return;
    for (auto it = d->children.begin(); it != d->children.end(); ++it) {
        if (it->get() == child.d.get()) {
            d->children.erase(it);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

namespace {

bool isNameStart(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalpha(u) || c == '_' || u >= 0x80;
}

bool isNameChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '-' || c == '.' || c == ':' ||
           u >= 0x80;
}

} // namespace

namespace detail {

class XmlParser {
public:
    explicit XmlParser(std::string text) : t_(std::move(text)) {}

    bool parse(XmlElement& root, String* error) {
        error_ = error;
        skipMisc();
        if (atEnd()) {
            fail("document is empty");
            return false;
        }
        if (!parseElement(root))
            return false;
        skipMisc();
        if (!atEnd()) {
            fail("unexpected content after the root element");
            return false;
        }
        return true;
    }

private:
    bool atEnd() const { return pos_ >= t_.size(); }
    char peek() const { return atEnd() ? '\0' : t_[pos_]; }
    bool startsWith(const char* what) const {
        const std::size_t len = std::char_traits<char>::length(what);
        return pos_ + len <= t_.size() && t_.compare(pos_, len, what, len) == 0;
    }

    void fail(const char* message) {
        if (!error_)
            return;
        int line = 1, column = 1;
        for (std::size_t i = 0; i < pos_ && i < t_.size(); ++i) {
            if (t_[i] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        *error_ = String("line ") + String::number(line) + ", column " +
                  String::number(column) + ": " + message;
    }

    void skipWhitespace() {
        while (!atEnd() &&
               std::isspace(static_cast<unsigned char>(peek())))
            ++pos_;
    }

    // Skips whitespace, comments, processing instructions and the doctype.
    void skipMisc() {
        for (;;) {
            skipWhitespace();
            if (startsWith("<!--")) {
                const std::size_t end = t_.find("-->", pos_ + 4);
                pos_ = end == std::string::npos ? t_.size() : end + 3;
            } else if (startsWith("<?")) {
                const std::size_t end = t_.find("?>", pos_ + 2);
                pos_ = end == std::string::npos ? t_.size() : end + 2;
            } else if (startsWith("<!DOCTYPE")) {
                const std::size_t end = t_.find('>', pos_ + 9);
                pos_ = end == std::string::npos ? t_.size() : end + 1;
            } else {
                return;
            }
        }
    }

    std::string parseName() {
        if (atEnd() || !isNameStart(peek()))
            return {};
        const std::size_t start = pos_++;
        while (!atEnd() && isNameChar(peek()))
            ++pos_;
        return t_.substr(start, pos_ - start);
    }

    // Appends the entity reference at pos_ (pointing past '&') to out.
    bool decodeEntity(std::string& out) {
        const std::size_t start = pos_;
        const std::size_t semi = t_.find(';', pos_);
        if (semi == std::string::npos || semi - start > 10) {
            fail("unterminated entity reference");
            return false;
        }
        const std::string name = t_.substr(start, semi - start);
        pos_ = semi + 1;
        if (name == "lt")
            out += '<';
        else if (name == "gt")
            out += '>';
        else if (name == "amp")
            out += '&';
        else if (name == "quot")
            out += '"';
        else if (name == "apos")
            out += '\'';
        else if (!name.empty() && name[0] == '#') {
            unsigned long code = 0;
            try {
                code = std::stoul(name.substr(1), nullptr,
                                  name.size() > 1 && (name[1] == 'x' ||
                                                      name[1] == 'X')
                                      ? 16
                                      : 10);
            } catch (...) {
                fail("invalid character reference");
                return false;
            }
            // Minimal UTF-8 encoding.
            if (code < 0x80) {
                out += static_cast<char>(code);
            } else if (code < 0x800) {
                out += static_cast<char>(0xC0 | (code >> 6));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                out += static_cast<char>(0xE0 | (code >> 12));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            } else {
                out += static_cast<char>(0xF0 | (code >> 18));
                out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (code & 0x3F));
            }
        } else {
            fail("unknown entity reference");
            return false;
        }
        return true;
    }

    // Parses text (entity-decoded) up to '<'. Returns false on error.
    bool parseText(std::string& out) {
        while (!atEnd() && peek() != '<') {
            if (peek() == '&') {
                ++pos_;
                if (!decodeEntity(out))
                    return false;
            } else {
                out += peek();
                ++pos_;
            }
        }
        return true;
    }

    bool parseAttributeValue(std::string& out) {
        const char quote = peek();
        if (quote != '"' && quote != '\'') {
            fail("attribute value must be quoted");
            return false;
        }
        ++pos_;
        while (!atEnd() && peek() != quote) {
            if (peek() == '<') {
                fail("'<' is not allowed in attribute values");
                return false;
            }
            if (peek() == '&') {
                ++pos_;
                if (!decodeEntity(out))
                    return false;
            } else {
                out += peek();
                ++pos_;
            }
        }
        if (atEnd()) {
            fail("unterminated attribute value");
            return false;
        }
        ++pos_; // closing quote
        return true;
    }

    bool parseElement(XmlElement& out) {
        if (peek() != '<' || startsWith("</") || startsWith("<!") ||
            startsWith("<?")) {
            fail("expected an element");
            return false;
        }
        ++pos_; // '<'
        const std::string tag = parseName();
        if (tag.empty()) {
            fail("expected an element name");
            return false;
        }
        auto node = std::make_shared<XmlElement::Node>();
        node->tag = String(tag);

        // Attributes.
        for (;;) {
            skipWhitespace();
            if (atEnd()) {
                fail("unexpected end of document inside a tag");
                return false;
            }
            if (peek() == '>' || peek() == '/')
                break;
            const std::string name = parseName();
            if (name.empty()) {
                fail("expected an attribute name");
                return false;
            }
            skipWhitespace();
            if (peek() != '=') {
                fail("expected '=' after the attribute name");
                return false;
            }
            ++pos_;
            skipWhitespace();
            std::string value;
            if (!parseAttributeValue(value))
                return false;
            node->attributes.emplace_back(String(name), String(value));
        }

        if (startsWith("/>")) {
            pos_ += 2;
            out = XmlElement(std::move(node));
            return true;
        }
        if (peek() != '>') {
            fail("expected '>' or '/>'");
            return false;
        }
        ++pos_;

        // Content: text, nested elements, comments, CDATA.
        for (;;) {
            if (atEnd()) {
                fail("unexpected end of document inside an element");
                return false;
            }
            if (startsWith("</")) {
                pos_ += 2;
                const std::string closing = parseName();
                skipWhitespace();
                if (peek() == '>')
                    ++pos_;
                if (closing != tag) {
                    fail("mismatched closing tag");
                    return false;
                }
                out = XmlElement(std::move(node));
                return true;
            }
            if (startsWith("<!--")) {
                const std::size_t end = t_.find("-->", pos_ + 4);
                if (end == std::string::npos) {
                    fail("unterminated comment");
                    return false;
                }
                pos_ = end + 3;
                continue;
            }
            if (startsWith("<![CDATA[")) {
                const std::size_t end = t_.find("]]>", pos_ + 9);
                if (end == std::string::npos) {
                    fail("unterminated CDATA section");
                    return false;
                }
                node->texts.push_back(
                    String(t_.substr(pos_ + 9, end - pos_ - 9)));
                pos_ = end + 3;
                continue;
            }
            if (startsWith("<?")) {
                const std::size_t end = t_.find("?>", pos_ + 2);
                pos_ = end == std::string::npos ? t_.size() : end + 2;
                continue;
            }
            if (peek() == '<') {
                XmlElement child;
                if (!parseElement(child))
                    return false;
                node->children.push_back(child.d);
                continue;
            }
            std::string chunk;
            if (!parseText(chunk))
                return false;
            if (!chunk.empty())
                node->texts.push_back(String(chunk));
        }
    }

    std::string t_;
    std::size_t pos_ = 0;
    String* error_ = nullptr;
};

} // namespace detail

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

namespace {

void escapeText(std::string& out, const std::string& text) {
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out += c; break;
        }
    }
}

void escapeAttribute(std::string& out, const std::string& text) {
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
}

void writeIndent(std::string& out, int level, bool indented) {
    if (indented)
        out.append(static_cast<std::size_t>(level) * 2, ' ');
}

bool isBlank(const String& s) {
    for (const char c : s.toStdString())
        if (!std::isspace(static_cast<unsigned char>(c)))
            return false;
    return true;
}

void writeElement(std::string& out, const XmlElement& element, int level,
                  bool indented) {
    writeIndent(out, level, indented);
    out += '<';
    out += element.tagName().toStdString();
    for (const String& name : element.attributeNames()) {
        out += ' ';
        out += name.toStdString();
        out += "=\"";
        escapeAttribute(out, element.attribute(name).toStdString());
        out += '"';
    }

    const std::vector<XmlElement> children = element.childElements();
    const String text = element.text();
    if (children.empty() && isBlank(text)) {
        out += "/>";
        if (indented)
            out += '\n';
        return;
    }
    out += '>';
    if (children.empty()) {
        // Pure text content stays on one line.
        escapeText(out, text.toStdString());
        out += "</";
        out += element.tagName().toStdString();
        out += '>';
        if (indented)
            out += '\n';
        return;
    }
    if (indented)
        out += '\n';
    if (!isBlank(text)) {
        writeIndent(out, level + 1, indented);
        escapeText(out, text.toStdString());
        if (indented)
            out += '\n';
    }
    for (const XmlElement& child : children)
        writeElement(out, child, level + 1, indented);
    writeIndent(out, level, indented);
    out += "</";
    out += element.tagName().toStdString();
    out += '>';
    if (indented)
        out += '\n';
}

} // namespace

// ---------------------------------------------------------------------------
// XmlDocument
// ---------------------------------------------------------------------------

struct XmlDocument::Impl {
    XmlElement root;
};

XmlDocument::XmlDocument() : d(std::make_unique<Impl>()) {}
XmlDocument::~XmlDocument() = default;
XmlDocument::XmlDocument(const XmlDocument& other)
    : d(std::make_unique<Impl>(*other.d)) {}
XmlDocument::XmlDocument(XmlDocument&&) noexcept = default;
XmlDocument& XmlDocument::operator=(const XmlDocument& other) {
    if (this != &other)
        d->root = other.d->root;
    return *this;
}
XmlDocument& XmlDocument::operator=(XmlDocument&&) noexcept = default;

XmlDocument XmlDocument::fromString(const String& xml, String* error) {
    XmlDocument doc;
    detail::XmlParser parser(xml.toStdString());
    if (!parser.parse(doc.d->root, error))
        return XmlDocument();
    return doc;
}

bool XmlDocument::isNull() const { return d->root.isNull(); }
XmlElement XmlDocument::rootElement() const { return d->root; }
void XmlDocument::setRootElement(const XmlElement& root) { d->root = root; }

XmlElement XmlDocument::createElement(const String& tagName) {
    auto node = std::make_shared<XmlElement::Node>();
    node->tag = tagName;
    return XmlElement(std::move(node));
}

String XmlDocument::toString(bool indented) const {
    if (d->root.isNull())
        return String();
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
    if (indented)
        out += '\n';
    writeElement(out, d->root, 0, indented);
    return String(out);
}

} // namespace Firmir


