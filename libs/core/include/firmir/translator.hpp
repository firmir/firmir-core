#pragma once

// Firmir::Translator — runtime string translation for UIs.
//
// Strings in code/Firml are written as trId("some-stable-id"); the actual
// wording per language comes from translation files (JSON):
//
//   { "language": "ru", "strings": { "some-stable-id": "Текст", ... } }
//
// A string value may also be an array of plural forms (gettext style):
//
//   "tasks-count": ["%n задача", "%n задачи", "%n задач"]
//
// trIdPlural(id, n) picks the form that matches the active language's
// plural rules and substitutes %n with the number; trId(id, args)
// substitutes %1..%9 in the (first) form. Language rules: Russian (and
// uk/be) use one/few/many, English and most others one/other.
//
// Files are added/removed at runtime (addTranslationFile /
// removeTranslationFile); the active language is the writable "language"
// property. Both changing the language and changing the file set emit
// propertyChanged("language"), so every `trId(...)` binding re-evaluates
// and all visible texts switch immediately — that is the property-mechanism
// integration the Firml side relies on.
//
// Unknown ids translate to the id itself, so the UI stays readable while
// translations catch up.
//
// The instance is a process-wide singleton registered under the name
// "Firmir.Core.Translator".

#include <firmir/object.hpp>
#include <firmir/string.hpp>

#include <initializer_list>
#include <memory>
#include <vector>

namespace Firmir {

class Translator : public Object {
public:
    /// Returns the process-wide translator instance (created on first use).
    static Translator* instance();

    /// Translates @p id into the active language; returns @p id unchanged
    /// when no translation is known. For ids with plural forms the first
    /// form is returned; no placeholder substitution is performed.
    String trId(const String& id) const;

    /// Translates @p id and replaces %1..%9 with the given arguments (in
    /// order). Placeholders without a matching argument are left as-is.
    String trId(const String& id, std::initializer_list<String> args) const;

    /// Translates @p id to the plural form for @p n (per the active
    /// language's rules) and replaces %n with the number. A plain string
    /// value gets only the %n substitution; an unknown id yields @p id.
    String trIdPlural(const String& id, long long n) const;

    /// The active language tag ("en", "ru", ...). The default follows
    /// Locale::language() (system language, or the application override set
    /// on Locale); calling setLanguage() detaches from the Locale.
    String language() const;
    void setLanguage(String language);

    /// Loads a translation file (see the format above). Returns false and
    /// logs a warning when the file cannot be read or parsed. Re-adding the
    /// same path replaces its strings.
    bool addTranslationFile(String path);
    /// Drops the strings that came from @p path. Returns false when the
    /// path was not registered.
    bool removeTranslationFile(String path);
    /// Paths of the currently loaded translation files.
    std::vector<String> translationFiles() const;

private:
    explicit Translator(Object* parent = nullptr);
    ~Translator() override;

    class Impl;
    std::unique_ptr<Impl> d;
};

/// Convenience free function: Translator::instance()->trId(id).
inline String trId(const String& id) { return Translator::instance()->trId(id); }
inline String trId(const char* id) { return Translator::instance()->trId(String(id)); }
inline String trId(const String& id, std::initializer_list<String> args) {
    return Translator::instance()->trId(id, args);
}
inline String trIdPlural(const String& id, long long n) {
    return Translator::instance()->trIdPlural(id, n);
}

} // namespace Firmir
