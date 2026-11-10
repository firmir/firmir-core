// firmir-wayland-generator — generate lightweight C++ Wayland protocol
// wrappers using only libwayland (a C++ port of the former Python script;
// the generated output is byte-compatible with it).
//
// Usage:
//   firmir-wayland-generator --mode client|server --protocol-header <header>
//                            <input.xml> <output.hpp>
//
// Exit codes: 0 = ok, 2 = usage/IO/XML error.

#include <firmir/xml.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

struct Arg {
    std::string name;
    std::string type;      // int, uint, fixed, string, array, fd, object, new_id
    std::string interface; // empty when absent
};

struct Message {
    std::string name;
    std::string type; // "destructor" or empty
    std::vector<Arg> args;
};

struct Interface {
    std::string name;
    std::vector<Message> requests;
    std::vector<Message> events;
};

std::string camel(const std::string& name) {
    std::string out;
    bool upperNext = true;
    for (const char c : name) {
        if (c == '_') {
            upperNext = true;
            continue;
        }
        out += upperNext ? static_cast<char>(std::toupper(static_cast<unsigned char>(c)))
                         : c;
        upperNext = false;
    }
    return out;
}

std::string className(const std::string& native) {
    return camel(native.rfind("wl_", 0) == 0 ? native.substr(3) : native);
}

std::string method(const std::string& name) {
    std::string value = camel(name);
    if (!value.empty())
        value[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[0])));
    return value;
}

std::string cppType(const Arg& arg) {
    if (arg.type == "int") return "std::int32_t";
    if (arg.type == "uint") return "std::uint32_t";
    if (arg.type == "fixed") return "wl_fixed_t";
    if (arg.type == "string") return "const char*";
    if (arg.type == "array") return "wl_array*";
    if (arg.type == "fd") return "int";
    if (arg.type == "object" || arg.type == "new_id")
        return (arg.interface.empty() ? "wl_proxy" : arg.interface) + "*";
    return "std::uint32_t";
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += sep;
        out += parts[i];
    }
    return out;
}

std::vector<Interface> parseProtocol(const Firmir::XmlElement& root) {
    std::vector<Interface> interfaces;
    for (const Firmir::XmlElement& ifaceEl : root.childElements("interface")) {
        Interface iface;
        iface.name = ifaceEl.attribute("name").toStdString();
        for (const char* tag : {"request", "event"}) {
            for (const Firmir::XmlElement& msgEl : ifaceEl.childElements(tag)) {
                Message msg;
                msg.name = msgEl.attribute("name").toStdString();
                msg.type = msgEl.attribute("type").toStdString();
                for (const Firmir::XmlElement& argEl : msgEl.childElements("arg")) {
                    Arg arg;
                    arg.name = argEl.attribute("name").toStdString();
                    arg.type = argEl.attribute("type").toStdString();
                    arg.interface = argEl.attribute("interface").toStdString();
                    msg.args.push_back(std::move(arg));
                }
                if (tag[0] == 'r')
                    iface.requests.push_back(std::move(msg));
                else
                    iface.events.push_back(std::move(msg));
            }
        }
        interfaces.push_back(std::move(iface));
    }
    return interfaces;
}

bool isSkipped(const std::string& native) {
    return native == "wl_display" || native == "wl_registry";
}

void emitClientEvents(std::string& out, const std::string& native,
                      const std::string& cls, const std::vector<Message>& events) {
    if (events.empty())
        return;
    out += "    struct Listener {\n";
    for (const Message& event : events) {
        std::vector<std::string> types;
        for (const Arg& arg : event.args)
            types.push_back(cppType(arg));
        out += "        std::function<void(" + join(types, ", ") + ")> " +
               method(event.name) + ";\n";
    }
    out += "    };\n    void setListener(Listener listener) {\n        m_listener = "
           "std::move(listener);\n";
    out += "        " + native + "_add_listener(handle(), &s_listener, this);\n    }\n";
    for (const Message& event : events) {
        std::vector<std::string> nativeArgParts;
        for (const Arg& arg : event.args)
            nativeArgParts.push_back(cppType(arg) + " " + arg.name);
        const std::string nativeArgs = join(nativeArgParts, ", ");
        const std::string prefix =
            "void* data, " + native + "*" + (nativeArgs.empty() ? "" : ", ");
        out += "    static void on_" + event.name + "(" + prefix + nativeArgs + ") {\n";
        out += "        auto* self = static_cast<" + cls + "*>(data);\n";
        std::vector<std::string> values;
        for (const Arg& arg : event.args)
            values.push_back(arg.name);
        out += "        if (self->m_listener." + method(event.name) +
               ") self->m_listener." + method(event.name) + "(" + join(values, ", ") +
               ");\n    }\n";
    }
    std::vector<std::string> entries;
    for (const Message& event : events)
        entries.push_back("&on_" + event.name);
    out += "    inline static const " + native + "_listener s_listener = {" +
           join(entries, ", ") + "};\n";
    out += "    Listener m_listener;\n";
}

std::string generateClient(const std::vector<Interface>& interfaces) {
    std::vector<std::string> deferred;
    std::string out;
    out += "// Generated by firmir-wayland-generator. Do not edit.\n#pragma once\n";
    out += "#include <firmir/wayland/client/proxy.hpp>\n";
    out += "#include <firmir/wayland/client/registry.hpp>\n";
    out += "#include <functional>\n#include <cstdint>\n#include <utility>\n\n";
    out += "namespace Firmir { namespace Wayland { namespace Client {\n\n";
    for (const Interface& iface : interfaces)
        if (!isSkipped(iface.name))
            out += "class " + className(iface.name) + ";\n";
    out += "\n";
    for (const Interface& iface : interfaces) {
        if (isSkipped(iface.name))
            continue;
        const std::string& native = iface.name;
        const std::string cls = className(native);
        std::string destructor;
        for (const Message& req : iface.requests)
            if (req.type == "destructor" && destructor.empty())
                destructor = method(req.name);
        out += "class " + cls + " : public Proxy<" + native + "> {\npublic:\n";
        out += "    using Native = " + native + ";\n";
        if (!destructor.empty())
            out += "    explicit " + cls + "(" + native + "* value = nullptr) : Proxy<" +
                   native + ">(value, &" + native + "_" + destructor + ") {}\n";
        else
            out += "    explicit " + cls + "(" + native + "* value = nullptr) : Proxy<" +
                   native + ">(value, [](" + native +
                   "* p) { wl_proxy_destroy(reinterpret_cast<wl_proxy*>(p)); }) {}\n";
        out += "    static " + cls +
               " bind(Registry& registry, std::uint32_t name, std::uint32_t version) {\n";
        out += "        return " + cls + "(static_cast<" + native +
               "*>(wl_registry_bind(registry.handle(), name, &" + native +
               "_interface, version)));\n    }\n";
        for (const Message& req : iface.requests) {
            if (req.type == "destructor")
                continue;
            std::vector<const Arg*> newIds;
            std::vector<const Arg*> ordinary;
            for (const Arg& arg : req.args) {
                if (arg.type == "new_id")
                    newIds.push_back(&arg);
                else
                    ordinary.push_back(&arg);
            }
            if (!newIds.empty() && newIds[0]->interface.empty())
                continue;
            const std::string result =
                newIds.empty() ? "void" : className(newIds[0]->interface);
            std::vector<std::string> paramParts;
            for (const Arg* arg : ordinary)
                paramParts.push_back(cppType(*arg) + " " + arg->name);
            const std::string params = join(paramParts, ", ");
            const std::string methodName = method(req.name);
            if (!newIds.empty()) {
                out += "    " + result + " " + methodName + "(" + params + ");\n";
                std::vector<std::string> callArgs{"handle()"};
                for (const Arg* arg : ordinary)
                    callArgs.push_back(arg->name);
                const std::string call =
                    native + "_" + req.name + "(" + join(callArgs, ", ") + ")";
                deferred.push_back("inline " + result + " " + cls + "::" + methodName +
                                   "(" + params + ") {\n");
                deferred.push_back("    return " + result + "(" + call + ");\n}\n");
                continue;
            }
            out += "    " + result + " " + methodName + "(" + params + ") {\n";
            std::vector<std::string> callArgs{"handle()"};
            for (const Arg* arg : ordinary)
                callArgs.push_back(arg->name);
            out += "        " + native + "_" + req.name + "(" + join(callArgs, ", ") +
                   ");\n    }\n";
        }
        emitClientEvents(out, native, cls, iface.events);
        out += "};\n\n";
    }
    for (const std::string& chunk : deferred)
        out += chunk;
    out += "}}} // namespace Firmir::Wayland::Client\n";
    return out;
}

std::string generateServer(const std::vector<Interface>& interfaces) {
    std::string out;
    out += "// Generated by firmir-wayland-generator. Do not edit.\n#pragma once\n";
    out += "#include <cstdint>\n#include <wayland-server-core.h>\n\n";
    out += "namespace Firmir { namespace Wayland { namespace Server {\n\n";
    for (const Interface& iface : interfaces) {
        const std::string& native = iface.name;
        if (isSkipped(native))
            continue;
        const std::string cls = className(native);
        out += "class " + cls + " {\npublic:\n";
        out += "    explicit " + cls +
               "(wl_resource* resource = nullptr) : m_resource(resource) {}\n";
        out += "    wl_resource* resource() const { return m_resource; }\n";
        out += "    static const wl_interface* interface() { return &" + native +
               "_interface; }\n";
        for (const Message& event : iface.events) {
            std::vector<std::string> paramParts;
            for (const Arg& arg : event.args)
                paramParts.push_back(cppType(arg) + " " + arg.name);
            std::vector<std::string> valueParts{"m_resource"};
            for (const Arg& arg : event.args)
                valueParts.push_back(arg.name);
            out += "    void " + method(event.name) + "(" + join(paramParts, ", ") +
                   ") {\n";
            out += "        if (m_resource) " + native + "_send_" + event.name + "(" +
                   join(valueParts, ", ") + ");\n    }\n";
        }
        out += "};\n\n";
    }
    out += "}}} // namespace Firmir::Wayland::Server\n";
    return out;
}

void printUsage(std::ostream& out) {
    out << "firmir-wayland-generator - generate C++ Wayland protocol wrappers\n"
           "\n"
           "Usage: firmir-wayland-generator --mode client|server\n"
           "                                --protocol-header <header>\n"
           "                                <input.xml> <output.hpp>\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string mode;
    std::string protocolHeader;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printUsage(std::cout);
            return 0;
        }
        if (arg == "--mode" || arg == "--protocol-header") {
            if (i + 1 >= argc) {
                std::cerr << "firmir-wayland-generator: " << arg << " needs a value\n";
                return 2;
            }
            (arg == "--mode" ? mode : protocolHeader) = argv[++i];
            continue;
        }
        if (!arg.empty() && arg[0] == '-') {
            std::cerr << "firmir-wayland-generator: unknown option '" << arg << "'\n";
            return 2;
        }
        positional.push_back(arg);
    }

    if ((mode != "client" && mode != "server") || protocolHeader.empty() ||
        positional.size() != 2) {
        printUsage(std::cerr);
        return 2;
    }

    std::ifstream in(positional[0]);
    if (!in) {
        std::cerr << "firmir-wayland-generator: cannot open '" << positional[0]
                  << "'\n";
        return 2;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();

    Firmir::String error;
    const Firmir::XmlDocument doc =
        Firmir::XmlDocument::fromString(Firmir::String(buffer.str()), &error);
    if (doc.isNull()) {
        std::cerr << "firmir-wayland-generator: cannot parse '" << positional[0]
                  << "': " << error.toStdString() << "\n";
        return 2;
    }

    const std::vector<Interface> interfaces = parseProtocol(doc.rootElement());
    std::string body = mode == "client" ? generateClient(interfaces)
                                        : generateServer(interfaces);
    const std::string anchor = "#pragma once\n";
    if (const size_t pos = body.find(anchor); pos != std::string::npos)
        body.insert(pos + anchor.size(), "#include <" + protocolHeader + ">\n");

    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(positional[1]).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, ec);
    std::ofstream outFile(positional[1]);
    if (!outFile) {
        std::cerr << "firmir-wayland-generator: cannot write '" << positional[1]
                  << "'\n";
        return 2;
    }
    outFile << body;
    return 0;
}

