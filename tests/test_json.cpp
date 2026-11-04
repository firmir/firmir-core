#include "test_framework.hpp"

#include <firmir/json.hpp>

using namespace Firmir;

TEST_CASE(json_value_types) {
    CHECK(JsonValue().isNull());
    CHECK(JsonValue::undefined().isUndefined());
    CHECK(JsonValue(true).isBool());
    CHECK(JsonValue(42).isDouble());
    CHECK(JsonValue("s").isString());
    CHECK(JsonValue(JsonArray()).isArray());
    CHECK(JsonValue(JsonObject()).isObject());

    CHECK_EQ(JsonValue("x").toString(), "x");
    CHECK_EQ(JsonValue(7).toInt(), 7);
    CHECK_EQ(JsonValue(7.5).toDouble(), 7.5);
    CHECK(JsonValue(true).toBool());

    // Defaults when the type does not match.
    CHECK_EQ(JsonValue(1).toString("def"), "def");
    CHECK_EQ(JsonValue("s").toInt(9), 9);
    CHECK(!JsonValue().toBool(false));
}

TEST_CASE(json_array) {
    JsonArray a;
    CHECK(a.isEmpty());
    a.append(1);
    a.append("two");
    a.prepend(0);
    a.insert(2, true);
    CHECK_EQ(a.size(), 4);
    CHECK_EQ(a.at(0).toInt(), 0);
    CHECK_EQ(a.at(1).toInt(), 1);
    CHECK(a.at(2).toBool());
    CHECK_EQ(a.at(3).toString(), "two");
    CHECK(a.at(99).isUndefined());

    a.removeAt(0);
    CHECK_EQ(a.size(), 3);
    CHECK_EQ(a.first().toInt(), 1);
    CHECK_EQ(a.last().toString(), "two");

    // Implicit sharing: copies share data until a mutation detaches.
    JsonArray shared = a;
    shared.removeAt(0);
    CHECK_EQ(shared.size(), 2);
    CHECK_EQ(a.size(), 3); // original untouched
}

TEST_CASE(json_object) {
    JsonObject o;
    o.insert("name", "firmir");
    o.insert("version", 2);
    o.insert("stable", true);
    CHECK_EQ(o.size(), 3);
    CHECK(o.contains("name"));
    CHECK(!o.contains("missing"));
    CHECK(o.value("missing").isUndefined());
    CHECK_EQ(o["name"].toString(), "firmir");
    CHECK_EQ(o["version"].toInt(), 2);
    CHECK(o["stable"].toBool());

    const auto keys = o.keys(); // sorted
    CHECK_EQ(keys.size(), 3u);
    CHECK_EQ(keys[0], "name");
    CHECK_EQ(keys[1], "stable");
    CHECK_EQ(keys[2], "version");

    o.remove("version");
    CHECK_EQ(o.size(), 2);
    CHECK(!o.contains("version"));
}

TEST_CASE(json_document_roundtrip) {
    JsonObject user;
    user.insert("name", "Ivan");
    user.insert("age", 30);

    JsonArray tags;
    tags.append("admin");
    tags.append("dev");

    JsonObject root;
    root.insert("user", user);
    root.insert("tags", tags);
    root.insert("active", true);
    root.insert("score", 9.5);
    root.insert("note", JsonValue());

    JsonDocument doc;
    doc.setObject(root);
    CHECK(doc.isObject());
    CHECK(!doc.isArray());

    const ByteArray compact = doc.toJson(JsonDocument::JsonFormat::Compact);
    const ByteArray pretty = doc.toJson(JsonDocument::JsonFormat::Indented);

    // Both formats must parse back to an equal structure.
    JsonDocument::ParseError err;
    const JsonDocument back1 = JsonDocument::fromJson(compact, &err);
    CHECK(err.error == JsonDocument::ParseError::NoError);
    CHECK(back1.object() == root);

    const JsonDocument back2 = JsonDocument::fromJson(pretty, &err);
    CHECK(err.error == JsonDocument::ParseError::NoError);
    CHECK(back2.object() == root);

    // And the parsed values are accessible.
    const JsonObject parsed = back1.object();
    CHECK_EQ(parsed["user"].toObject()["name"].toString(), "Ivan");
    CHECK_EQ(parsed["tags"].toArray().size(), 2);
    CHECK_EQ(parsed["tags"].toArray().at(1).toString(), "dev");
    CHECK_EQ(parsed["score"].toDouble(), 9.5);
    CHECK(parsed["note"].isNull());
}

TEST_CASE(json_parse_edge_cases) {
    JsonDocument::ParseError err;

    // Whitespace, escapes and UTF-8 \u sequences.
    const JsonDocument doc =
        JsonDocument::fromJson(" [ 1, \"a\\n\\u00e9\\u4e2d\", null ] ", &err);
    CHECK(err.error == JsonDocument::ParseError::NoError);
    CHECK(doc.isArray());
    CHECK_EQ(doc.array().at(0).toInt(), 1);
    CHECK_EQ(doc.array().at(1).toString(), String("a\n\xc3\xa9\xe4\xb8\xad"));
    CHECK(doc.array().at(2).isNull());

    // Errors are reported with an offset and yield a null document.
    const JsonDocument bad1 = JsonDocument::fromJson("{bad", &err);
    CHECK(err.error == JsonDocument::ParseError::SyntaxError);
    CHECK(err.offset >= 0);
    CHECK(bad1.isNull());

    const JsonDocument bad2 = JsonDocument::fromJson("[1,]", &err);
    CHECK(err.error == JsonDocument::ParseError::SyntaxError);
    CHECK(bad2.isNull());

    const JsonDocument bad3 = JsonDocument::fromJson("1 2", &err);
    CHECK(err.error == JsonDocument::ParseError::SyntaxError);

    // Compact output of a known structure.
    JsonObject o;
    o.insert("a", 1);
    o.insert("b", "x");
    JsonDocument d;
    d.setObject(o);
    CHECK_EQ(d.toJson(JsonDocument::JsonFormat::Compact),
             "{\"a\":1,\"b\":\"x\"}");

    // Null document serializes to nothing, like Qt.
    CHECK(JsonDocument().toJson().isEmpty());

    // Values survive equality comparison.
    CHECK(JsonValue(5) == JsonValue(5));
    CHECK(JsonValue(5) != JsonValue(6));
    CHECK(JsonValue("a") != JsonValue(5));
}
