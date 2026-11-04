#include "test_framework.hpp"

#include <firmir/xml.hpp>

#include <vector>

using namespace Firmir;

TEST_CASE(xml_parse_basic) {
    String error;
    const XmlDocument doc = XmlDocument::fromString(
        "<?xml version=\"1.0\"?>\n"
        "<greeting lang=\"en\">hello</greeting>\n",
        &error);
    CHECK(!doc.isNull());
    CHECK(error.isEmpty());

    const XmlElement root = doc.rootElement();
    CHECK_EQ(root.tagName(), "greeting");
    CHECK_EQ(root.attribute("lang"), "en");
    CHECK_EQ(root.attribute("missing", "fallback"), "fallback");
    CHECK(root.hasAttribute("lang"));
    CHECK(!root.hasAttribute("missing"));
    CHECK_EQ(root.text(), "hello");
}

TEST_CASE(xml_nested_and_search) {
    const XmlDocument doc = XmlDocument::fromString(
        "<node name=\"root\">"
        "  <node name=\"a\"><node name=\"a1\"/></node>"
        "  <node name=\"b\"/>"
        "  <other/>"
        "</node>");
    CHECK(!doc.isNull());

    const XmlElement root = doc.rootElement();
    CHECK_EQ(root.childElements().size(), 3u);
    CHECK_EQ(root.childElements("node").size(), 2u);
    CHECK_EQ(root.firstChildElement().attribute("name"), "a");
    CHECK_EQ(root.firstChildElement("other").tagName(), "other");
    CHECK(root.firstChildElement("absent").isNull());

    const std::vector<XmlElement> all = root.elementsByTagName("node");
    CHECK_EQ(all.size(), 3u); // a, a1, b (depth-first)
    CHECK_EQ(all.at(1).attribute("name"), "a1");
}

TEST_CASE(xml_attributes_entities_cdata_comments) {
    const XmlDocument doc = XmlDocument::fromString(
        "<r a=\"x&lt;y&quot; z\">"
        "<!-- a comment -->"
        "text &amp; &lt;entities&gt;"
        "<![CDATA[<raw> & data]]>"
        "<sub/>"
        "</r>");
    CHECK(!doc.isNull());
    const XmlElement root = doc.rootElement();
    CHECK_EQ(root.attribute("a"), "x<y\" z");
    CHECK_EQ(root.text(), "text & <entities><raw> & data");
    CHECK_EQ(root.childElements().size(), 1u);
}

TEST_CASE(xml_writer_and_roundtrip) {
    XmlDocument doc;
    doc.setRootElement(XmlDocument::createElement("config"));
    XmlElement root = doc.rootElement();

    XmlElement server = XmlDocument::createElement("server");
    server.setAttribute("host", "local&host");
    server.setAttribute("port", "8080");
    root.appendChild(server);

    XmlElement note = XmlDocument::createElement("note");
    note.setText("use <wisely> & \"quotes\"");
    root.appendChild(note);

    XmlElement empty = XmlDocument::createElement("empty");
    root.appendChild(empty);

    const String text = doc.toString();
    CHECK(text.indexOf("<?xml version=\"1.0\"") == 0);
    CHECK(text.indexOf("host=\"local&amp;host\"") > 0);
    CHECK(text.indexOf("use &lt;wisely&gt; &amp; \"quotes\"") > 0);
    CHECK(text.indexOf("<empty/>") > 0);

    // Round-trip: parse the serialized form back.
    String error;
    const XmlDocument copy = XmlDocument::fromString(text, &error);
    CHECK(!copy.isNull());
    CHECK(error.isEmpty());
    const XmlElement serverCopy = copy.rootElement().firstChildElement("server");
    CHECK_EQ(serverCopy.attribute("host"), "local&host");
    CHECK_EQ(copy.rootElement().firstChildElement("note").text(),
             "use <wisely> & \"quotes\"");

    // Compact form has no newlines.
    CHECK(doc.toString(false).indexOf("\n") < 0);
}

TEST_CASE(xml_implicit_sharing_and_mutation) {
    XmlDocument doc = XmlDocument::fromString("<root><item id=\"1\"/></root>");
    XmlElement item = doc.rootElement().firstChildElement("item");
    XmlElement alias = item; // QDom semantics: the same node.
    alias.setAttribute("id", "2");
    CHECK_EQ(item.attribute("id"), "2");
    CHECK_EQ(doc.rootElement().firstChildElement("item").attribute("id"), "2");

    alias.removeAttribute("id");
    CHECK(!item.hasAttribute("id"));

    item.setText("body");
    CHECK_EQ(item.text(), "body");

    XmlElement extra = XmlDocument::createElement("extra");
    doc.rootElement().appendChild(extra);
    CHECK_EQ(doc.rootElement().childElements().size(), 2u);
    doc.rootElement().removeChild(extra);
    CHECK_EQ(doc.rootElement().childElements().size(), 1u);
}

TEST_CASE(xml_errors) {
    String error;
    CHECK(XmlDocument::fromString("", &error).isNull());
    CHECK(!error.isEmpty());

    error.clear();
    CHECK(XmlDocument::fromString("<a><b></a></b>", &error).isNull());
    CHECK(error.indexOf("mismatched") >= 0);

    error.clear();
    CHECK(XmlDocument::fromString("<a><b/></a>extra", &error).isNull());

    error.clear();
    CHECK(XmlDocument::fromString("<a>&nope;</a>", &error).isNull());

    error.clear();
    CHECK(XmlDocument::fromString("<a x=noquote/>", &error).isNull());
}
