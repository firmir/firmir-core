#include <firmir/settings.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <system_error>

namespace Firmir {

namespace {

/// Escapes characters that would break the INI format.
std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else if (c == '\\')
            out += "\\\\";
        else
            out += c;
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char next = s[++i];
            if (next == 'n')
                out += '\n';
            else if (next == 'r')
                out += '\r';
            else
                out += next;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::string trim(const std::string& s) {
    const std::size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    const std::size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

/// Creates parent directories of @p path (like `mkdir -p`).
bool mkpath(const std::string& path) {
    const std::filesystem::path parent =
        std::filesystem::path(path).parent_path();
    if (parent.empty())
        return true;
    std::error_code ec;
    return std::filesystem::create_directories(parent, ec) ||
           std::filesystem::is_directory(parent);
}

std::string defaultFilePath(const String& organization,
                            const String& application) {
#ifdef _WIN32
    // %APPDATA%/<org>/<app>.ini (fallback: <USERPROFILE>/AppData/Roaming).
    const char* appData = std::getenv("APPDATA");
    std::string base;
    if (appData && *appData) {
        base = appData;
    } else {
        const char* profile = std::getenv("USERPROFILE");
        base = std::string(profile ? profile : ".") + "/AppData/Roaming";
    }
#else
    const char* configHome = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::string base =
        configHome && *configHome
            ? std::string(configHome)
            : (std::string(home ? home : ".") + "/.config");
#endif
    return base + "/" + organization.toStdString() + "/" +
           application.toStdString() + ".ini";
}

} // namespace

struct Settings::Impl {
    String filePath;
    std::map<std::string, std::string> entries; // flat key ("grp/key") -> value
    std::vector<String> groupStack;
    bool dirty = false;

    explicit Impl(String path) : filePath(std::move(path)) { load(); }

    String prefix() const {
        std::string out;
        for (const String& g : groupStack) {
            out += g.toStdString();
            out += '/';
        }
        return String(out);
    }

    std::string fullKey(const String& key) const {
        return (prefix() + key).toStdString();
    }

    void load() {
        entries.clear();
        FILE* file = std::fopen(filePath.toStdString().c_str(), "r");
        if (!file)
            return;
        std::string group;
        char lineBuf[4096];
        while (std::fgets(lineBuf, sizeof(lineBuf), file)) {
            std::string line = trim(lineBuf);
            if (line.empty() || line.front() == '#')
                continue;
            if (line.front() == '[' && line.back() == ']') {
                group = trim(line.substr(1, line.size() - 2));
                continue;
            }
            const std::size_t eq = line.find('=');
            if (eq == std::string::npos)
                continue;
            std::string key = trim(line.substr(0, eq));
            std::string value = unescape(trim(line.substr(eq + 1)));
            if (!group.empty())
                key = group + "/" + key;
            entries[key] = value;
        }
        std::fclose(file);
    }

    bool save() const {
        const std::string path = filePath.toStdString();
        const std::size_t slash = path.find_last_of('/');
        if (slash != std::string::npos && !mkpath(path.substr(0, slash + 1)))
            return false;

        FILE* file = std::fopen(path.c_str(), "w");
        if (!file)
            return false;

        std::string currentGroup;
        for (const auto& [key, value] : entries) {
            const std::size_t slashPos = key.find_last_of('/');
            const std::string group = slashPos == std::string::npos
                                          ? std::string()
                                          : key.substr(0, slashPos);
            const std::string leaf =
                slashPos == std::string::npos ? key : key.substr(slashPos + 1);
            if (group != currentGroup) {
                if (!currentGroup.empty())
                    std::fputc('\n', file);
                if (!group.empty())
                    std::fprintf(file, "[%s]\n", group.c_str());
                currentGroup = group;
            }
            std::fprintf(file, "%s = %s\n", leaf.c_str(), escape(value).c_str());
        }
        std::fclose(file);
        return true;
    }
};

Settings::Settings(const String& organization, const String& application)
    : d(std::make_unique<Impl>(defaultFilePath(organization, application))) {}

Settings::Settings(const String& filePath)
    : d(std::make_unique<Impl>(filePath)) {}

Settings::~Settings() { sync(); }

Variant Settings::value(const String& key, const Variant& defaultValue) const {
    const auto it = d->entries.find(d->fullKey(key));
    if (it == d->entries.end())
        return defaultValue;
    return Variant(String(it->second));
}

void Settings::setValue(const String& key, const Variant& value) {
    d->entries[d->fullKey(key)] = value.toString().toStdString();
    d->dirty = true;
}

void Settings::remove(const String& key) {
    const std::string full = d->fullKey(key);
    const std::string prefix = full + "/";
    bool removed = d->entries.erase(full) > 0;
    for (auto it = d->entries.begin(); it != d->entries.end();) {
        if (it->first.compare(0, prefix.size(), prefix) == 0) {
            it = d->entries.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    d->dirty = d->dirty || removed;
}

bool Settings::contains(const String& key) const {
    return d->entries.find(d->fullKey(key)) != d->entries.end();
}

void Settings::clear() {
    if (!d->entries.empty()) {
        d->entries.clear();
        d->dirty = true;
    }
}

void Settings::beginGroup(const String& prefix) {
    d->groupStack.push_back(prefix);
}

void Settings::endGroup() {
    if (!d->groupStack.empty())
        d->groupStack.pop_back();
}

String Settings::group() const {
    return d->groupStack.empty() ? String() : d->groupStack.back();
}

std::vector<String> Settings::childKeys() const {
    const std::string prefix = d->prefix().toStdString();
    std::vector<String> keys;
    for (const auto& [key, value] : d->entries) {
        (void)value;
        if (key.compare(0, prefix.size(), prefix) != 0)
            continue;
        const std::string rest = key.substr(prefix.size());
        if (rest.find('/') == std::string::npos)
            keys.emplace_back(rest);
    }
    return keys;
}

std::vector<String> Settings::childGroups() const {
    const std::string prefix = d->prefix().toStdString();
    std::vector<String> groups;
    for (const auto& [key, value] : d->entries) {
        (void)value;
        if (key.compare(0, prefix.size(), prefix) != 0)
            continue;
        const std::string rest = key.substr(prefix.size());
        const std::size_t slash = rest.find('/');
        if (slash == std::string::npos)
            continue;
        const std::string group = rest.substr(0, slash);
        bool seen = false;
        for (const String& g : groups)
            seen = seen || g.toStdString() == group;
        if (!seen)
            groups.emplace_back(group);
    }
    return groups;
}

bool Settings::isWritable() const {
    const std::string path = d->filePath.toStdString();
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        // Opening for append does not truncate and proves writability.
        if (std::FILE* f = std::fopen(path.c_str(), "ab")) {
            std::fclose(f);
            return true;
        }
        return false;
    }
    // The file may not exist yet; probe the parent directory by creating a
    // throwaway sibling file.
    const std::string probe = path + ".probe";
    if (std::FILE* f = std::fopen(probe.c_str(), "wb")) {
        std::fclose(f);
        std::filesystem::remove(probe, ec);
        return true;
    }
    return false;
}

String Settings::fileName() const { return d->filePath; }

bool Settings::sync() {
    if (!d->dirty)
        return true;
    if (!d->save())
        return false;
    d->dirty = false;
    d->load(); // pull in changes made by other Settings objects
    return true;
}

} // namespace Firmir
