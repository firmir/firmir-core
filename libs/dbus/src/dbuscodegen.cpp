#include <firmir/dbuscodegen.hpp>

#include <firmir/xml.hpp>

namespace Firmir {

// ---------------------------------------------------------------------------
// Introspection XML parsing
// ---------------------------------------------------------------------------

std::vector<DBusInterfaceInfo> parseInterfacesXml(const String& xml,
                                                  String* error) {
    std::vector<DBusInterfaceInfo> out;
    String parseError;
    const XmlDocument doc = XmlDocument::fromString(xml, &parseError);
    if (doc.isNull()) {
        if (error)
            *error = parseError;
        return out;
    }
    for (const XmlElement& ifaceEl :
         doc.rootElement().elementsByTagName("interface")) {
        DBusInterfaceInfo iface;
        iface.name = ifaceEl.attribute("name");
        for (const XmlElement& methodEl : ifaceEl.childElements("method")) {
            DBusMethodInfo method;
            method.name = methodEl.attribute("name");
            for (const XmlElement& argEl : methodEl.childElements("arg")) {
                DBusArgInfo arg;
                arg.name = argEl.attribute("name");
                arg.signature = argEl.attribute("type").toStdString();
                const String direction = argEl.attribute("direction", "in");
                if (direction == "out")
                    method.outArgs.push_back(std::move(arg));
                else
                    method.inArgs.push_back(std::move(arg));
            }
            iface.methods.push_back(std::move(method));
        }
        for (const XmlElement& signalEl : ifaceEl.childElements("signal")) {
            DBusSignalInfo signal;
            signal.name = signalEl.attribute("name");
            for (const XmlElement& argEl : signalEl.childElements("arg")) {
                DBusArgInfo arg;
                arg.name = argEl.attribute("name");
                arg.signature = argEl.attribute("type").toStdString();
                signal.args.push_back(std::move(arg));
            }
            iface.signals.push_back(std::move(signal));
        }
        out.push_back(std::move(iface));
    }
    if (out.empty() && error)
        *error = "no <interface> elements found";
    return out;
}

String defaultClassName(const DBusInterfaceInfo& interface,
                        const String& suffix) {
    const std::string name = interface.name.toStdString();
    const std::size_t dot = name.rfind('.');
    return String(dot == std::string::npos ? name : name.substr(dot + 1)) +
           suffix;
}

// ---------------------------------------------------------------------------
// Type mapping
// ---------------------------------------------------------------------------

namespace {

std::string cppType(const std::string& signature) {
    if (signature == "y") return "std::uint8_t";
    if (signature == "b") return "bool";
    if (signature == "n") return "std::int16_t";
    if (signature == "q") return "std::uint16_t";
    if (signature == "i") return "std::int32_t";
    if (signature == "u") return "std::uint32_t";
    if (signature == "x") return "std::int64_t";
    if (signature == "t") return "std::uint64_t";
    if (signature == "d") return "double";
    if (signature == "s" || signature == "o" || signature == "g")
        return "Firmir::String";
    if (signature == "as") return "std::vector<Firmir::String>";
    if (signature == "ai") return "std::vector<std::int32_t>";
    if (signature == "ay") return "Firmir::ByteArray";
    return "Firmir::DBusValue";
}

// C++ expression converting a value of the given signature to DBusValue.
std::string toValueExpr(const std::string& expr, const std::string& sig) {
    if (sig == "as")
        return "::firmir_dbus_detail::toValueAs(" + expr + ")";
    if (sig == "ai")
        return "::firmir_dbus_detail::toValueAi(" + expr + ")";
    if (sig == "ay")
        return "::firmir_dbus_detail::toValueAy(" + expr + ")";
    return "Firmir::DBusValue(" + expr + ")";
}

// C++ expression extracting a C++ value of the given signature.
std::string fromValueExpr(const std::string& expr, const std::string& sig) {
    if (sig == "y") return expr + ".toUInt8()";
    if (sig == "b") return expr + ".toBool()";
    if (sig == "n") return expr + ".toInt16()";
    if (sig == "q") return expr + ".toUInt16()";
    if (sig == "i") return expr + ".toInt32()";
    if (sig == "u") return expr + ".toUInt32()";
    if (sig == "x") return expr + ".toInt64()";
    if (sig == "t") return expr + ".toUInt64()";
    if (sig == "d") return expr + ".toDouble()";
    if (sig == "s" || sig == "o" || sig == "g") return expr + ".toString()";
    if (sig == "as") return "::firmir_dbus_detail::fromValueAs(" + expr + ")";
    if (sig == "ai") return "::firmir_dbus_detail::fromValueAi(" + expr + ")";
    if (sig == "ay") return "::firmir_dbus_detail::fromValueAy(" + expr + ")";
    return expr; // generic DBusValue
}

std::string argName(const DBusArgInfo& arg, int index) {
    return arg.name.isEmpty() ? "arg" + std::to_string(index)
                              : arg.name.toStdString();
}

std::string helpersPreamble() {
    return "#ifndef FIRMIR_DBUS_DETAIL_HELPERS_HPP\n"
           "#define FIRMIR_DBUS_DETAIL_HELPERS_HPP\n"
           "namespace firmir_dbus_detail {\n"
           "inline Firmir::DBusValue toValueAs(const std::vector<Firmir::String>& v) {\n"
           "    std::vector<Firmir::DBusValue> items;\n"
           "    for (const auto& e : v) items.push_back(Firmir::DBusValue(e));\n"
           "    return Firmir::DBusValue::makeArray(\"s\", std::move(items));\n"
           "}\n"
           "inline std::vector<Firmir::String> fromValueAs(const Firmir::DBusValue& v) {\n"
           "    std::vector<Firmir::String> out;\n"
           "    if (const auto* a = v.asArray())\n"
           "        for (const auto& e : a->values) out.push_back(e.toString());\n"
           "    return out;\n"
           "}\n"
           "inline Firmir::DBusValue toValueAi(const std::vector<std::int32_t>& v) {\n"
           "    std::vector<Firmir::DBusValue> items;\n"
           "    for (const auto& e : v) items.push_back(Firmir::DBusValue(e));\n"
           "    return Firmir::DBusValue::makeArray(\"i\", std::move(items));\n"
           "}\n"
           "inline std::vector<std::int32_t> fromValueAi(const Firmir::DBusValue& v) {\n"
           "    std::vector<std::int32_t> out;\n"
           "    if (const auto* a = v.asArray())\n"
           "        for (const auto& e : a->values) out.push_back(e.toInt32());\n"
           "    return out;\n"
           "}\n"
           "inline Firmir::DBusValue toValueAy(const Firmir::ByteArray& v) {\n"
           "    std::vector<Firmir::DBusValue> items;\n"
           "    for (char c : v.toStdString())\n"
           "        items.push_back(Firmir::DBusValue(static_cast<std::uint8_t>(c)));\n"
           "    return Firmir::DBusValue::makeArray(\"y\", std::move(items));\n"
           "}\n"
           "inline Firmir::ByteArray fromValueAy(const Firmir::DBusValue& v) {\n"
           "    std::string out;\n"
           "    if (const auto* a = v.asArray())\n"
           "        for (const auto& e : a->values) out += static_cast<char>(e.toUInt8());\n"
           "    return Firmir::ByteArray(out);\n"
           "}\n"
           "} // namespace firmir_dbus_detail\n"
           "#endif // FIRMIR_DBUS_DETAIL_HELPERS_HPP\n\n";
}

std::string proxyMethod(const DBusInterfaceInfo& iface,
                        const DBusMethodInfo& method) {
    const std::string name = method.name.toStdString();
    std::string out = "    ";
    // Signature: single out arg -> return value; several -> out parameters.
    std::string decl;
    std::string params;
    int i = 0;
    for (const DBusArgInfo& arg : method.inArgs) {
        if (!params.empty())
            params += ", ";
        params += cppType(arg.signature) + " " + argName(arg, i);
        ++i;
    }
    if (method.outArgs.empty()) {
        decl = "void " + name + "(" + params + ")";
    } else if (method.outArgs.size() == 1) {
        decl = cppType(method.outArgs[0].signature) + " " + name + "(" +
               params + ")";
    } else {
        std::string outParams;
        int j = 0;
        for (const DBusArgInfo& arg : method.outArgs) {
            if (!outParams.empty())
                outParams += ", ";
            outParams +=
                cppType(arg.signature) + "& " + argName(arg, j);
            ++j;
        }
        decl = "void " + name + "(" + params +
               (params.empty() ? "" : ", ") + outParams + ")";
    }
    out += decl + " {\n";
    out += "        Firmir::DBusMessage call_ = Firmir::DBusMessage::createMethodCall(\n"
           "            service_, path_, \"" +
           iface.name.toStdString() + "\", \"" + name + "\");\n";
    out += "        call_.setArguments({";
    i = 0;
    bool first = true;
    for (const DBusArgInfo& arg : method.inArgs) {
        if (!first)
            out += ", ";
        out += toValueExpr(argName(arg, i), arg.signature);
        first = false;
        ++i;
    }
    out += "});\n";
    out += "        Firmir::DBusMessage reply_ = connection_.call(call_);\n";
    for (std::size_t j = 0; j < method.outArgs.size(); ++j) {
        const DBusArgInfo& arg = method.outArgs[j];
        const std::string expr =
            "reply_.arguments().at(" + std::to_string(j) + ")";
        if (method.outArgs.size() == 1)
            out += "        return " + fromValueExpr(expr, arg.signature) +
                   ";\n";
        else
            out += "        " + argName(arg, static_cast<int>(j)) + " = " +
                   fromValueExpr(expr, arg.signature) + ";\n";
    }
    out += "    }\n";
    return out;
}

std::string proxySignalConnector(const DBusInterfaceInfo& iface,
                                 const DBusSignalInfo& signal) {
    const std::string name = signal.name.toStdString();
    std::string params;
    int i = 0;
    for (const DBusArgInfo& arg : signal.args) {
        if (!params.empty())
            params += ", ";
        params += cppType(arg.signature) + " " + argName(arg, i);
        ++i;
    }
    std::string out = "    void connect" + name +
                      "(std::function<void(" + params + ")> handler) {\n";
    out += "        connection_.subscribeSignal(\"" + iface.name.toStdString() +
           "\", \"" + name + "\",\n";
    out += "            [handler = std::move(handler)](const Firmir::DBusMessage& msg_) {\n";
    out += "                handler(";
    i = 0;
    for (const DBusArgInfo& arg : signal.args) {
        if (i)
            out += ", ";
        out += fromValueExpr("msg_.arguments().at(" + std::to_string(i) + ")",
                             arg.signature);
        ++i;
    }
    out += ");\n            });\n    }\n";
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Generators
// ---------------------------------------------------------------------------

String generateProxy(const DBusInterfaceInfo& interface,
                     const String& classNameArg) {
    const std::string className =
        (classNameArg.isEmpty() ? defaultClassName(interface, "Proxy")
                                : classNameArg)
            .toStdString();
    std::string out = "#pragma once\n\n"
                      "// Generated by firmir-dbus-tool --proxy.\n"
                      "#include <firmir/dbusconnection.hpp>\n\n"
                      "#include <cstdint>\n#include <functional>\n#include "
                      "<utility>\n#include <vector>\n\n";
    out += helpersPreamble();
    out += "class " + className + " {\npublic:\n";
    out += "    " + className +
           "(Firmir::DBusConnection& connection, Firmir::String service,\n"
           "     Firmir::String path)\n"
           "        : connection_(connection), service_(std::move(service)),\n"
           "          path_(std::move(path)) {}\n\n";
    out += "    static constexpr const char* interfaceName() {\n"
           "        return \"" +
           interface.name.toStdString() + "\";\n    }\n\n";
    for (const DBusMethodInfo& method : interface.methods)
        out += proxyMethod(interface, method) + "\n";
    for (const DBusSignalInfo& signal : interface.signals)
        out += proxySignalConnector(interface, signal) + "\n";
    out += "private:\n"
           "    Firmir::DBusConnection& connection_;\n"
           "    Firmir::String service_;\n"
           "    Firmir::String path_;\n"
           "};\n";
    return String(out);
}

String generateAdaptor(const DBusInterfaceInfo& interface,
                       const String& classNameArg) {
    const std::string className =
        (classNameArg.isEmpty() ? defaultClassName(interface, "Adaptor")
                                : classNameArg)
            .toStdString();
    std::string out = "#pragma once\n\n"
                      "// Generated by firmir-dbus-tool --adaptor.\n"
                      "#include <firmir/dbusconnection.hpp>\n\n"
                      "#include <cstdint>\n#include <utility>\n#include "
                      "<vector>\n\n";
    out += helpersPreamble();
    out += "class " + className + " {\npublic:\n";
    out += "    virtual ~" + className + "() = default;\n\n";

    for (const DBusMethodInfo& method : interface.methods) {
        std::string params;
        int i = 0;
        for (const DBusArgInfo& arg : method.inArgs) {
            if (!params.empty())
                params += ", ";
            params += cppType(arg.signature) + " " + argName(arg, i);
            ++i;
        }
        const std::string name = method.name.toStdString();
        if (method.outArgs.empty()) {
            out += "    virtual void " + name + "(" + params + ") = 0;\n";
        } else if (method.outArgs.size() == 1) {
            out += "    virtual " + cppType(method.outArgs[0].signature) +
                   " " + name + "(" + params + ") = 0;\n";
        } else {
            int j = 0;
            for (const DBusArgInfo& arg : method.outArgs) {
                params += (params.empty() ? "" : ", ") +
                          cppType(arg.signature) + "& " + argName(arg, j);
                ++j;
            }
            out += "    virtual void " + name + "(" + params + ") = 0;\n";
        }
    }
    if (!interface.methods.empty())
        out += "\n";

    // interfaceInfo() for DBusConnection::registerObject().
    out += "    static Firmir::DBusInterfaceInfo interfaceInfo() {\n"
           "        Firmir::DBusInterfaceInfo info;\n"
           "        info.name = \"" +
           interface.name.toStdString() + "\";\n";
    for (const DBusMethodInfo& method : interface.methods) {
        out += "        info.methods.push_back({\"" +
               method.name.toStdString() + "\", {";
        int i = 0;
        bool first = true;
        for (const DBusArgInfo& arg : method.inArgs) {
            if (!first)
                out += ", ";
            out += "{\"" + argName(arg, i) + "\", \"" + arg.signature +
                   "\"}";
            first = false;
            ++i;
        }
        out += "}, {";
        int j = 0;
        first = true;
        for (const DBusArgInfo& arg : method.outArgs) {
            if (!first)
                out += ", ";
            out += "{\"" + argName(arg, j) + "\", \"" + arg.signature +
                   "\"}";
            first = false;
            ++j;
        }
        out += "}});\n";
    }
    for (const DBusSignalInfo& signal : interface.signals) {
        out += "        info.signals.push_back({\"" +
               signal.name.toStdString() + "\", {";
        int i = 0;
        bool first = true;
        for (const DBusArgInfo& arg : signal.args) {
            if (!first)
                out += ", ";
            out += "{\"" + argName(arg, i) + "\", \"" + arg.signature +
                   "\"}";
            first = false;
            ++i;
        }
        out += "}});\n";
    }
    out += "        return info;\n    }\n";

    // dispatch() for DBusConnection::registerObject().
    out += "\n    Firmir::DBusMessage dispatch(const Firmir::DBusMessage& call_) {\n";
    for (const DBusMethodInfo& method : interface.methods) {
        const std::string name = method.name.toStdString();
        out += "        if (call_.member() == \"" + name + "\") {\n";
        std::string unpack;
        for (std::size_t j = 0; j < method.inArgs.size(); ++j) {
            if (!unpack.empty())
                unpack += ", ";
            unpack += fromValueExpr("call_.arguments().at(" +
                                        std::to_string(j) + ")",
                                    method.inArgs[j].signature);
        }
        if (method.outArgs.empty()) {
            out += "            " + name + "(" + unpack + ");\n"
                   "            return "
                   "Firmir::DBusMessage::createMethodReturn(call_);\n";
        } else if (method.outArgs.size() == 1) {
            out += "            Firmir::DBusMessage reply_ =\n"
                   "                Firmir::DBusMessage::createMethodReturn(call_);\n"
                   "            reply_.setArguments({" +
                   toValueExpr(name + "(" + unpack + ")",
                               method.outArgs[0].signature) +
                   "});\n            return reply_;\n";
        } else {
            out += "            Firmir::DBusMessage reply_ =\n"
                   "                Firmir::DBusMessage::createMethodReturn(call_);\n";
            for (std::size_t j = 0; j < method.outArgs.size(); ++j)
                out += "            " + cppType(method.outArgs[j].signature) +
                       " out" + std::to_string(j) + "{};\n";
            out += "            " + name + "(" + unpack;
            for (std::size_t j = 0; j < method.outArgs.size(); ++j)
                out += std::string(unpack.empty() && j == 0 ? "" : ", ") +
                       "out" + std::to_string(j);
            out += ");\n            reply_.setArguments({";
            for (std::size_t j = 0; j < method.outArgs.size(); ++j)
                out += std::string(j ? ", " : "") +
                       toValueExpr("out" + std::to_string(j),
                                   method.outArgs[j].signature);
            out += "});\n            return reply_;\n";
        }
        out += "        }\n";
    }
    out += "        return Firmir::DBusMessage::createError(\n"
           "            call_, \"org.freedesktop.DBus.Error.UnknownMethod\",\n"
           "            Firmir::String(\"no such method on ";
    out += interface.name.toStdString();
    out += ": \") + call_.member());\n"
           "    }\n";
    out += "};\n";
    return String(out);
}

} // namespace Firmir
