#pragma once

// Firmir XML — a small DOM-style XML API (own parser, no deps).
//
//   Firmir::XmlDocument doc = Firmir::XmlDocument::fromString(xml, &error);
//   Firmir::XmlElement root = doc.rootElement();
//   for (const Firmir::XmlElement& item : root.childElements("item"))
//       use(item.attribute("id"), item.text());
//
// XmlElement is an implicitly shared handle: copies refer to the same node,
// so mutating a copy mutates the document (handle semantics). A null element
// (default-constructed, or a failed lookup) reads as empty and ignores
// mutations.

#include <firmir/string.hpp>

#include <memory>
#include <vector>

namespace Firmir {

namespace detail {
class XmlParser; // Implementation detail of XmlDocument::fromString().
}

class XmlElement {
public:
    XmlElement(); // null handle

    bool isNull() const;

    String tagName() const;

    /// Attribute access. attribute() returns @p defaultValue when absent.
    String attribute(const String& name,
                     const String& defaultValue = String()) const;
    bool hasAttribute(const String& name) const;
    std::vector<String> attributeNames() const;
    void setAttribute(const String& name, const String& value);
    void removeAttribute(const String& name);

    /// Concatenated character data directly inside this element.
    String text() const;
    /// Replaces all character data and child elements with a text node.
    void setText(const String& text);

    /// Direct child elements (optionally filtered by tag name).
    std::vector<XmlElement> childElements() const;
    std::vector<XmlElement> childElements(const String& tagName) const;
    /// First direct child with the given tag (any tag when empty); null when
    /// absent.
    XmlElement firstChildElement(const String& tagName = String()) const;
    /// All descendant elements with the given tag name, depth-first.
    std::vector<XmlElement> elementsByTagName(const String& tagName) const;

    void appendChild(const XmlElement& child);
    void removeChild(const XmlElement& child);

private:
    struct Node;
    explicit XmlElement(std::shared_ptr<Node> node);
    std::shared_ptr<Node> d;
    friend class XmlDocument;
    friend class detail::XmlParser;
};

class XmlDocument {
public:
    XmlDocument(); // empty document without a root element
    ~XmlDocument();
    XmlDocument(const XmlDocument& other);
    XmlDocument(XmlDocument&& other) noexcept;
    XmlDocument& operator=(const XmlDocument& other);
    XmlDocument& operator=(XmlDocument&& other) noexcept;

    /// Parses @p xml. On failure returns a null document and, when
    /// @p error is given, fills it with "line X, column Y: message".
    static XmlDocument fromString(const String& xml, String* error = nullptr);

    bool isNull() const;
    XmlElement rootElement() const;
    /// Associates @p root with the document.
    void setRootElement(const XmlElement& root);

    /// Creates a detached element; appendChild() inserts it somewhere.
    static XmlElement createElement(const String& tagName);

    /// Serializes the document. Indented output uses two spaces per level.
    String toString(bool indented = true) const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    friend class detail::XmlParser;
};

} // namespace Firmir
