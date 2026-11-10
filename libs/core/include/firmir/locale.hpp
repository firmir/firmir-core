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

#include <memory>

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
    String systemLanguage() const;

    /// True while language() tracks the system language (no override set).
    bool followsSystemLanguage() const;

    /// Installs the application language override and stops following the
    /// system language. An empty or unchanged value is ignored.
    void setLanguage(String language);

    /// The loaded backend plugin instance (nullptr when none was found and
    /// the process-locale fallback is in use). Exposed for diagnostics and
    /// tests (e.g. the test backend is driven through dynamic properties).
    LocaleBackend* backend() const;

private:
    explicit Locale(Object* parent = nullptr);
    ~Locale() override;

    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
