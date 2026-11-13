// XML example: parsing, navigating, modifying and serializing XmlDocument.

#include <firmir/log.hpp>
#include <firmir/xml.hpp>

namespace {

const Firmir::LoggingCategory& lcApp() {
    static const Firmir::LoggingCategory category("app.xml");
    return category;
}

} // namespace

int main() {
    using namespace Firmir;

    // Parse a document; errors are reported as "line X, column Y: message".
    const String source(
        "<library>\n"
        "  <book id=\"1\">\n"
        "    <title>Firmir in Practice</title>\n"
        "    <author>M. Ivanov</author>\n"
        "  </book>\n"
        "  <book id=\"2\">\n"
        "    <title>Modern C++</title>\n"
        "  </book>\n"
        "</library>\n");
    String error;
    const XmlDocument doc = XmlDocument::fromString(source, &error);
    if (doc.isNull()) {
        cLogError(lcApp()) << "parse failed: " << error;
        return 1;
    }

    // Navigate: root element, child lookup, attributes, text content.
    // (XmlElement is a handle: mutating a copy mutates the
    // document, so the handle itself does not need to be const here.)
    XmlElement root = doc.rootElement();
    cLogInfo(lcApp()) << ("root: <" + root.tagName() + ">");
    for (const XmlElement& book : root.childElements("book")) {
        const String title = book.firstChildElement("title").text();
        const String author = book.firstChildElement("author").text();
        cLogInfo(lcApp()) << "book"
                          << ("#" + book.attribute("id") + ": '" + title +
                              "'")
                          << (author.isEmpty() ? " (no author)"
                                              : " by " + author);
    }

    // Modify: append a new element built with createElement().
    XmlElement book = XmlDocument::createElement("book");
    book.setAttribute("id", "3");
    XmlElement title = XmlDocument::createElement("title");
    title.setText("Patterns");
    book.appendChild(title);
    root.appendChild(book);

    cLogInfo(lcApp()) << "after modification:\n" << doc.toString();

    // Round-trip stability: parse the serialized form again.
    const XmlDocument reparsed = XmlDocument::fromString(doc.toString());
    cLogInfo(lcApp()) << "round-trip equal: "
                      << (reparsed.toString() == doc.toString());
    return reparsed.toString() == doc.toString() ? 0 : 1;
}
