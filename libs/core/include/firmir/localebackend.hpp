#pragma once

// Backend API for Firmir::Locale (see locale.hpp). A backend reports the
// system language and may notify about changes at runtime through the
// signalLanguageChanged signal (argument: String newLanguage).
//
// Backends are plugins: firmir-locale-<name>.so built against this header,
// exporting
//
//   extern "C" const Firmir::LocalePlugin* firmir_locale_plugin();
//
// Plugin search order: FIRMIR_CORE_PLUGIN_PATH (colon-separated), then
// "plugins/core" relative to the running executable, then the system plugin
// directory. FIRMIR_LOCALE_BACKEND=<name> pins a single backend by name.

#include <firmir/object.hpp>
#include <firmir/string.hpp>

namespace Firmir {

class LocaleBackend : public Object {
public:
    explicit LocaleBackend(Object* parent = nullptr) : Object(parent) {}

    /// The current system language tag ("en", "ru", ...).
    virtual String language() const = 0;

    /// Emitted when the system language changes: (String newLanguage).
    /// The Locale singleton re-reads language() and re-emits its own
    /// propertyChanged("language") unless an application override is set.
    static constexpr auto signalLanguageChanged = "languageChanged";
};

#define FIRMIR_LOCALE_PLUGIN_ABI 1
#define FIRMIR_LOCALE_PLUGIN_ENTRY "firmir_locale_plugin"

/// Plugin ABI descriptor exported by firmir-locale-<name>.so.
struct LocalePlugin {
    int abiVersion = FIRMIR_LOCALE_PLUGIN_ABI;
    /// Backend name ("test", ...); FIRMIR_LOCALE_BACKEND selects by it.
    const char* name = nullptr;
    /// Cheap availability probe; called before create().
    bool (*available)() = nullptr;
    /// Creates the backend as a child of @p host (the Locale singleton).
    LocaleBackend* (*create)(Object* host) = nullptr;
};

using LocalePluginEntry = const LocalePlugin* (*)();

} // namespace Firmir
