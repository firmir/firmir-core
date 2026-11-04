#include <firmir/dbusmessage.hpp>

#include "detail/marshall.hpp"

#include <cstring>

namespace Firmir {

// D-Bus header field codes.
namespace {
constexpr std::uint8_t kFieldPath = 1;        // 'o'
constexpr std::uint8_t kFieldInterface = 2;   // 's'
constexpr std::uint8_t kFieldMember = 3;      // 's'
constexpr std::uint8_t kFieldErrorName = 4;   // 's'
constexpr std::uint8_t kFieldReplySerial = 5; // 'u'
constexpr std::uint8_t kFieldDestination = 6; // 's'
constexpr std::uint8_t kFieldSender = 7;      // 's'
constexpr std::uint8_t kFieldSignature = 8;   // 'g'

DBusValue headerField(std::uint8_t code, DBusValue value) {
    return DBusValue::makeStruct(
        {DBusValue(code), DBusValue::makeVariant(std::move(value))});
}
} // namespace

struct DBusMessage::Impl {
    Type type = Type::Invalid;
    String path;
    String interface;
    String member;
    String destination;
    String sender;
    String errorName;
    std::uint32_t serial = 0;
    std::uint32_t replySerial = 0;
    std::vector<DBusValue> arguments;
};

DBusMessage::DBusMessage() : Object(nullptr), d(std::make_unique<Impl>()) {
    clearAffinity(); // Value messages may be created and destroyed on any thread.
}
DBusMessage::~DBusMessage() = default;
DBusMessage::DBusMessage(const DBusMessage& other)
    : Object(nullptr), d(std::make_unique<Impl>(*other.d)) {
    clearAffinity();
}
DBusMessage::DBusMessage(DBusMessage&& other) noexcept
    : Object(nullptr), d(std::move(other.d)) {
    clearAffinity();
}
DBusMessage& DBusMessage::operator=(const DBusMessage& other) {
    if (this != &other)
        *d = *other.d;
    return *this;
}
DBusMessage& DBusMessage::operator=(DBusMessage&& other) noexcept {
    if (this != &other)
        d = std::move(other.d);
    return *this;
}

DBusMessage::Type DBusMessage::type() const { return d->type; }

String DBusMessage::path() const { return d->path; }
String DBusMessage::interface() const { return d->interface; }
String DBusMessage::member() const { return d->member; }
String DBusMessage::destination() const { return d->destination; }
String DBusMessage::sender() const { return d->sender; }
String DBusMessage::errorName() const { return d->errorName; }

void DBusMessage::setSender(const String& sender) { d->sender = sender; }
void DBusMessage::setDestination(const String& destination) {
    d->destination = destination;
}

std::uint32_t DBusMessage::serial() const { return d->serial; }
void DBusMessage::setSerial(std::uint32_t serial) { d->serial = serial; }
std::uint32_t DBusMessage::replySerial() const { return d->replySerial; }
void DBusMessage::setReplySerial(std::uint32_t serial) {
    d->replySerial = serial;
}

const std::vector<DBusValue>& DBusMessage::arguments() const {
    return d->arguments;
}
void DBusMessage::setArguments(std::vector<DBusValue> arguments) {
    d->arguments = std::move(arguments);
}

DBusMessage DBusMessage::createMethodCall(const String& destination,
                                          const String& path,
                                          const String& interface,
                                          const String& method) {
    DBusMessage msg;
    msg.d->type = Type::MethodCall;
    msg.d->destination = destination;
    msg.d->path = path;
    msg.d->interface = interface;
    msg.d->member = method;
    return msg;
}

DBusMessage DBusMessage::createMethodReturn(const DBusMessage& call) {
    DBusMessage msg;
    msg.d->type = Type::MethodReturn;
    msg.d->destination = call.d->sender;
    msg.d->replySerial = call.d->serial;
    return msg;
}

DBusMessage DBusMessage::createError(const DBusMessage& call,
                                     const String& errorName,
                                     const String& message) {
    DBusMessage msg;
    msg.d->type = Type::Error;
    msg.d->destination = call.d->sender;
    msg.d->replySerial = call.d->serial;
    msg.d->errorName = errorName;
    msg.d->arguments.push_back(DBusValue(message));
    return msg;
}

DBusMessage DBusMessage::createSignal(const String& path,
                                      const String& interface,
                                      const String& name) {
    DBusMessage msg;
    msg.d->type = Type::Signal;
    msg.d->path = path;
    msg.d->interface = interface;
    msg.d->member = name;
    return msg;
}

std::string DBusMessage::argumentsSignature() const {
    std::string sig;
    for (const DBusValue& arg : d->arguments)
        sig += arg.signatureString();
    return sig;
}

ByteArray DBusMessage::serialize() const {
    // Body first: marshalling a fresh buffer keeps body alignment correct
    // because the body always starts at an 8-aligned offset in the message.
    std::string body;
    detail::DBusMarshaller bodyMarshaller(body);
    for (const DBusValue& arg : d->arguments)
        bodyMarshaller.writeValue(arg);

    std::vector<DBusValue> fields;
    if (!d->path.isEmpty())
        fields.push_back(headerField(kFieldPath, DBusValue::objectPath(d->path)));
    if (!d->interface.isEmpty())
        fields.push_back(headerField(kFieldInterface, DBusValue(d->interface)));
    if (!d->member.isEmpty())
        fields.push_back(headerField(kFieldMember, DBusValue(d->member)));
    if (!d->errorName.isEmpty())
        fields.push_back(headerField(kFieldErrorName, DBusValue(d->errorName)));
    if (d->replySerial != 0)
        fields.push_back(
            headerField(kFieldReplySerial, DBusValue(d->replySerial)));
    if (!d->destination.isEmpty())
        fields.push_back(headerField(kFieldDestination, DBusValue(d->destination)));
    if (!d->sender.isEmpty())
        fields.push_back(headerField(kFieldSender, DBusValue(d->sender)));
    const std::string sig = argumentsSignature();
    if (!sig.empty())
        fields.push_back(
            headerField(kFieldSignature, DBusValue::signature(String(sig))));

    std::string out;
    detail::DBusMarshaller marshaller(out);
    marshaller.writeValue(DBusValue(std::uint8_t('l')));        // endianness
    marshaller.writeValue(DBusValue(std::uint8_t(d->type)));      // type
    marshaller.writeValue(DBusValue(std::uint8_t(0)));          // flags
    marshaller.writeValue(DBusValue(std::uint8_t(1)));          // version
    marshaller.writeValue(
        DBusValue(std::uint32_t(body.size())));                 // body length
    marshaller.writeValue(DBusValue(std::uint32_t(d->serial)));   // serial
    marshaller.writeValue(
        DBusValue::makeArray("(yv)", std::move(fields)));       // header fields
    marshaller.pad(8);
    out += body;
    return ByteArray(out);
}

bool DBusMessage::parse(const char* data, std::size_t size, DBusMessage& out,
                        std::size_t& consumed, String* error) {
    consumed = 0;
    auto fail = [&](const char* message) {
        if (error)
            *error = String(message);
        return false;
    };
    if (size < 16)
        return false; // Incomplete fixed header.
    if (data[0] != 'l')
        return fail("unsupported byte order (only little-endian)");
    const int type = static_cast<std::uint8_t>(data[1]);
    if (type < 1 || type > 4)
        return fail("invalid message type");
    if (data[3] != 1)
        return fail("unsupported protocol version");

    std::uint32_t bodyLength = 0;
    std::uint32_t serial = 0;
    std::memcpy(&bodyLength, data + 4, 4);
    std::memcpy(&serial, data + 8, 4);

    detail::DBusUnmarshaller reader(data, size);
    reader.setPosition(12); // the fields array starts at offset 12
    DBusValue fieldsValue;
    std::size_t sigPos = 0;
    if (!reader.readValue("a(yv)", sigPos, fieldsValue))
        return false; // Incomplete header fields (or malformed; size wins).

    std::size_t bodyStart = reader.position();
    if (bodyStart % 8)
        bodyStart += 8 - bodyStart % 8;
    const std::size_t total = bodyStart + bodyLength;
    if (size < total)
        return false; // Incomplete body.

    DBusMessage msg;
    msg.d->type = static_cast<Type>(type);
    msg.d->serial = serial;

    const auto* fields = fieldsValue.asArray();
    if (!fields)
        return fail("malformed header fields");
    for (const DBusValue& fieldValue : fields->values) {
        const auto* field = fieldValue.asStruct();
        if (!field || field->values.size() != 2)
            return fail("malformed header field");
        const std::uint8_t code = field->values[0].toUInt8();
        const auto* box = field->values[1].asVariant();
        if (!box || !box->value)
            return fail("malformed header field variant");
        const DBusValue& value = *box->value;
        switch (code) {
        case kFieldPath:
            msg.d->path = value.toString();
            break;
        case kFieldInterface:
            msg.d->interface = value.toString();
            break;
        case kFieldMember:
            msg.d->member = value.toString();
            break;
        case kFieldErrorName:
            msg.d->errorName = value.toString();
            break;
        case kFieldReplySerial:
            msg.d->replySerial = value.toUInt32();
            break;
        case kFieldDestination:
            msg.d->destination = value.toString();
            break;
        case kFieldSender:
            msg.d->sender = value.toString();
            break;
        case kFieldSignature: {
            // Parsed below from the body.
            const std::string bodySig = value.toString().toStdString();
            detail::DBusUnmarshaller bodyReader(data, total);
            bodyReader.setPosition(bodyStart);
            std::size_t bodySigPos = 0;
            while (bodySigPos < bodySig.size()) {
                DBusValue arg;
                if (!bodyReader.readValue(bodySig, bodySigPos, arg))
                    return fail("malformed message body");
                msg.d->arguments.push_back(std::move(arg));
            }
            break;
        }
        default:
            break; // Unknown fields are ignored.
        }
    }

    consumed = total;
    out = std::move(msg);
    return true;
}

DBusError dbusErrorFromMessage(const DBusMessage& message) {
    String text;
    if (!message.arguments().empty())
        text = message.arguments().front().toString();
    return DBusError(message.errorName(), text);
}

} // namespace Firmir
