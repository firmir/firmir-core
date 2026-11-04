// JSON example: building, serializing and parsing Firmir JSON documents.

#include <firmir/json.hpp>
#include <firmir/log.hpp>

namespace {

const Firmir::LoggingCategory& lcApp() {
    static const Firmir::LoggingCategory category("app.json");
    return category;
}

} // namespace

int main() {
    using namespace Firmir;

    // Build a document from scratch (implicitly shared value classes).
    JsonObject object;
    object.insert("greeting", String("hello, firmir"));
    object.insert("answer", 42);
    JsonArray tags;
    tags.append(String("demo"));
    tags.append(String("firmir"));
    object.insert("tags", tags);

    JsonDocument document;
    document.setObject(object);
    cLogInfo(lcApp()) << "compact: "
                      << String(document.toJson(JsonDocument::JsonFormat::Compact)
                                    .toStdString());
    cLogInfo(lcApp()) << "indented:\n"
                      << String(document.toJson().toStdString());

    // Round-trip: parse the serialized form back and compare.
    const JsonDocument reparsed = JsonDocument::fromJson(document.toJson());
    cLogInfo(lcApp()) << "round-trip equal: " << (reparsed.object() == object);

    // Parsing error reporting (code + byte offset).
    JsonDocument::ParseError error;
    const JsonDocument broken =
        JsonDocument::fromJson(ByteArray("{\"a\": }"), &error);
    cLogInfo(lcApp()) << "broken document is null: " << broken.isNull()
                      << ", error: " << error.errorString() << " at offset "
                      << error.offset;

    return reparsed.object() == object && broken.isNull() ? 0 : 1;
}
