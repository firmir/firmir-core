#pragma once

// Firmir D-Bus code generation: parses D-Bus introspection XML (via
// firmir-xml) and generates C++ proxy (client) and adaptor (service) classes.
// Used by the firmir-dbus-tool command line utility; also usable directly.

#include <firmir/dbusconnection.hpp>

namespace Firmir {

/// Parses all <interface> elements of an introspection document.
/// On failure returns an empty list and fills @p error when given.
std::vector<DBusInterfaceInfo> parseInterfacesXml(const String& xml,
                                                  String* error = nullptr);

/// "org.example.Test" + "Proxy" -> "TestProxy".
String defaultClassName(const DBusInterfaceInfo& interface,
                        const String& suffix);

/// Generates a client proxy class (header content, without include guards).
String generateProxy(const DBusInterfaceInfo& interface,
                     const String& className = String());

/// Generates a service adaptor base class (pure virtual methods + a
/// dispatch() suitable for DBusConnection::registerObject()).
String generateAdaptor(const DBusInterfaceInfo& interface,
                       const String& className = String());

} // namespace Firmir
