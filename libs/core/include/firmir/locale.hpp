#pragma once

// Firmir::Locale — the current language ("en", "ru", ...) with two sources:
//
//   - the system language, reported by a pluggable backend
//     (firmir-locale-<name>.so, see localebackend.hpp; without a backend the
//     process locale FIRMIR_LANGUAGE / LC_ALL / LANG is parsed), and
//   - an application override set with setLanguage().
//
// While no override is set, Locale follows the backend: its
// signalLanguageChanged (propertyChanged("language")) updates language()
// live. Calling setLanguage() installs the application override and stops
// following the system language; language() then returns the override and
// backend notifications are ignored.
//
// The instance is a process-wide singleton registered under the name
// "Firmir.Core.Locale". See doc/core/locale.md.

#include <firmir/object.hpp>
#include <firmir/string.hpp>

#include <string>

namespace Firmir {

class LocaleBackend;

class Locale : public Object {
public:
    /// Returns the process-wide locale instance (created on first use).
    static Locale* instance();

    /// The effective language: the application override when set, otherwise
    /// the system language reported by the backend (or the process locale).
    String language() const;

    /// The system language (backend / process locale), ignoring the
    /// application override.
    String systemLanguage() const { return String(m_systemLanguage); }

    /// True while language() tracks the system language (no override set).
    bool followsSystemLanguage() const { return m_appLanguage.empty(); }

    /// Installs the application language override and stops following the
    /// system language. An empty or unchanged value is ignored.
    void setLanguage(String language);

    /// The loaded backend plugin instance (nullptr when none was found and
    /// the process-locale fallback is in use). Exposed for diagnostics and
    /// tests (e.g. the test backend is driven through dynamic properties).
    LocaleBackend* backend() const { return m_backend; }

private:
    explicit Locale(Object* parent = nullptr);

    void onSystemLanguageChanged(const String& language);

    std::string m_systemLanguage;
    std::string m_appLanguage; // empty while following the system
    LocaleBackend* m_backend = nullptr; // child
    Connection m_backendConnection;
};

} // namespace Firmir
