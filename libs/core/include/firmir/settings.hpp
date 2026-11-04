#pragma once

// Firmir::Settings — persistent application settings, the analogue of
// QSettings. Stores Variant values in a simple text format:
//
//   [Group/SubGroup]
//   key = value
//
// One line per entry, UTF-8, '#' starts a comment. The file format is
// INI-like, same as QSettings::IniFormat. Settings sync automatically on
// destruction; call sync() to force a write.

#include <firmir/string.hpp>
#include <firmir/variant.hpp>

#include <memory>
#include <vector>

namespace Firmir {

/// Persistent application settings, the Firmir analogue of QSettings.
/// Values are stored in a per-user INI file:
/// ~/.config/<organization>/<application>.ini
class Settings {
public:
    /// Settings scoped to the given organization and application.
    Settings(const String& organization, const String& application);
    /// Settings bound to an explicit INI file path.
    explicit Settings(const String& filePath);
    /// Writes pending changes to disk.
    ~Settings();

    Settings(const Settings&) = delete;
    Settings& operator=(const Settings&) = delete;

    /// Returns a value; defaultValue is returned when the key is missing.
    Variant value(const String& key,
                  const Variant& defaultValue = Variant()) const;
    /// Stores a value. The change is pending until sync()/destruction.
    void setValue(const String& key, const Variant& value);
    /// Removes a key (or a whole group).
    void remove(const String& key);
    /// Returns true when the key exists.
    bool contains(const String& key) const;
    /// Removes all keys.
    void clear();

    /// Starts a group: subsequent relative keys are prefixed with
    /// "prefix/".
    void beginGroup(const String& prefix);
    /// Ends the current group.
    void endGroup();
    /// The current group prefix ("" when no group is active).
    String group() const;
    /// Direct child keys of the current group (or root keys without '/').
    std::vector<String> childKeys() const;
    /// Direct child groups of the current group.
    std::vector<String> childGroups() const;

    /// Returns true when settings are stored on a writable file backend.
    bool isWritable() const;
    /// The file path this Settings object stores to.
    String fileName() const;
    /// Forces a write to the file. Returns false on failure.
    bool sync();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
