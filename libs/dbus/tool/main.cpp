// firmir-dbus-tool — generates C++ D-Bus proxy/adaptor classes from
// introspection XML.
//
// Usage:
//   firmir-dbus-tool --proxy   <input.xml> [--class Name] [-o output.hpp]
//   firmir-dbus-tool --adaptor <input.xml> [--class Name] [-o output.hpp]
//
// Without -o the generated code goes to stdout.

#include <firmir/dbuscodegen.hpp>
#include <firmir/file.hpp>

#include <cstdio>
#include <cstring>

namespace {

int usage() {
    std::fprintf(stderr,
                 "usage: firmir-dbus-tool --proxy|--adaptor <input.xml> "
                 "[--class Name] [-o output.hpp]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    bool proxy = false;
    bool adaptor = false;
    Firmir::String inputPath;
    Firmir::String outputPath;
    Firmir::String className;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--proxy") == 0)
            proxy = true;
        else if (std::strcmp(argv[i], "--adaptor") == 0)
            adaptor = true;
        else if (std::strcmp(argv[i], "--class") == 0 && i + 1 < argc)
            className = argv[++i];
        else if (std::strcmp(argv[i], "-o") == 0 && i + 1 < argc)
            outputPath = argv[++i];
        else if (argv[i][0] != '-')
            inputPath = argv[i];
        else
            return usage();
    }
    if (proxy == adaptor || inputPath.isEmpty())
        return usage();

    Firmir::File input(inputPath);
    if (!input.open(Firmir::File::ReadOnly)) {
        std::fprintf(stderr, "firmir-dbus-tool: cannot open %s: %s\n",
                     inputPath.toStdString().c_str(),
                     input.errorString().toStdString().c_str());
        return 1;
    }
    const Firmir::String xml = input.readAll().toStdString();

    Firmir::String error;
    const std::vector<Firmir::DBusInterfaceInfo> interfaces =
        Firmir::parseInterfacesXml(xml, &error);
    if (interfaces.empty()) {
        std::fprintf(stderr, "firmir-dbus-tool: %s\n",
                     error.toStdString().c_str());
        return 1;
    }

    Firmir::String generated;
    for (const Firmir::DBusInterfaceInfo& iface : interfaces)
        generated += proxy ? Firmir::generateProxy(iface, className)
                           : Firmir::generateAdaptor(iface, className);

    if (outputPath.isEmpty()) {
        std::fputs(generated.toStdString().c_str(), stdout);
        return 0;
    }
    Firmir::File output(outputPath);
    if (!output.open(Firmir::File::WriteOnly | Firmir::File::Truncate)) {
        std::fprintf(stderr, "firmir-dbus-tool: cannot write %s: %s\n",
                     outputPath.toStdString().c_str(),
                     output.errorString().toStdString().c_str());
        return 1;
    }
    output.write(Firmir::ByteArray(generated.toStdString()));
    return 0;
}
