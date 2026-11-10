// Firmir::Locale implementation: system language via a pluggable backend
// (firmir-locale-<name>.so) with the process locale as the fallback, plus
// the application override (setLanguage). See doc/core/locale.md.

#include <firmir/locale.hpp>

#include <firmir/localebackend.hpp>
#include <firmir/log.hpp>

#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>

#include <cstdlib>
#include <vector>

namespace Firmir {

namespace {

const LoggingCategory& lcLocale() {
    static const LoggingCategory category("locale");
    return category;
}

/// Directories searched for firmir-locale-<name>.so plugins.
std::vector<std::string> pluginDirs() {
    std::vector<std::string> dirs;
    const char* path = std::getenv("FIRMIR_CORE_PLUGIN_PATH");
    if (path && *path) {
        std::string rest = path;
        for (;;) {
            const std::size_t colon = rest.find(':');
            dirs.push_back(rest.substr(0, colon));
            if (colon == std::string::npos)
                break;
            rest.erase(0, colon + 1);
        }
    }
    char exePath[4096];
    const ssize_t len =
        ::readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        std::string dir = exePath;
        const std::size_t slash = dir.rfind('/');
        if (slash != std::string::npos) {
            dir.erase(slash);
            dirs.push_back(dir + "/../plugins/core");
            dirs.push_back(dir + "/../../plugins/core");
            dirs.push_back(dir + "/../../../plugins/core");
            dirs.push_back(dir + "/plugins/core");
        }
    }
#ifdef FIRMIR_SYSTEM_PLUGIN_DIR
    dirs.push_back(std::string(FIRMIR_SYSTEM_PLUGIN_DIR));
#endif
    return dirs;
}

/// The fallback system language from the process locale (LC_ALL / LANG,
/// e.g. "ru_RU.UTF-8" -> "ru"); "en" when unset.
std::string envLanguage() {
    const char* env = std::getenv("LC_ALL");
    if (!env || !*env)
        env = std::getenv("LANG");
    if (!env || !*env)
        return "en";
    std::string locale(env);
    // Strip codeset/modifier first: "C.UTF-8", "ru_RU.UTF-8@latin".
    const std::size_t dot = locale.find_first_of(".@");
    if (dot != std::string::npos)
        locale.resize(dot);
    const std::size_t underscore = locale.find('_');
    if (underscore != std::string::npos)
        locale.resize(underscore);
    if (locale.empty() || locale == "C" || locale == "POSIX")
        return "en";
    return locale;
}

/// Tries to load firmir-locale-<name>.so from every search directory.
LocaleBackend* loadBackendNamed(const std::string& name, Object* host) {
    const std::string file = "firmir-locale-" + name + ".so";
    for (const std::string& dir : pluginDirs()) {
        const std::string path = dir + "/" + file;
        void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle)
            continue;
        auto entry = reinterpret_cast<LocalePluginEntry>(
            dlsym(handle, FIRMIR_LOCALE_PLUGIN_ENTRY));
        const LocalePlugin* plugin = entry ? entry() : nullptr;
        if (!plugin || plugin->abiVersion != FIRMIR_LOCALE_PLUGIN_ABI) {
            cLogWarning(lcLocale()) << "bad plugin ABI in" << path;
            dlclose(handle);
            continue;
        }
        if (plugin->available && !plugin->available()) {
            cLogInfo(lcLocale()) << "locale backend unavailable:" << name;
            dlclose(handle);
            continue;
        }
        if (!plugin->create) {
            dlclose(handle);
            continue;
        }
        cLogInfo(lcLocale()) << "loaded locale backend" << path;
        return plugin->create(host);
    }
    return nullptr;
}

/// Picks a backend: FIRMIR_LOCALE_BACKEND pins a name; otherwise the first
/// firmir-locale-*.so found in the search directories.
LocaleBackend* loadBackend(Object* host) {
    const char* pinned = std::getenv("FIRMIR_LOCALE_BACKEND");
    if (pinned && *pinned)
        return loadBackendNamed(pinned, host);
    for (const std::string& dir : pluginDirs()) {
        DIR* d = opendir(dir.c_str());
        if (!d)
            continue;
        while (const dirent* entry = readdir(d)) {
            const std::string file = entry->d_name;
            if (file.rfind("firmir-locale-", 0) != 0 ||
                file.compare(file.size() - 3, 3, ".so") != 0)
                continue;
            const std::string name =
                file.substr(14, file.size() - 14 - 3); // strip prefix + ".so"
            if (LocaleBackend* backend = loadBackendNamed(name, host)) {
                closedir(d);
                return backend;
            }
        }
        closedir(d);
    }
    return nullptr;
}

} // namespace

class Locale::Impl {
public:
    explicit Impl(Locale* q) : q(q) {}

    void onSystemLanguageChanged(const String& language);

    Locale* q;
    std::string systemLanguage;
    std::string appLanguage; // empty while following the system
    LocaleBackend* backend = nullptr; // child
    Connection backendConnection;
};

Locale::Locale(Object* parent) : Object(parent), d(std::make_unique<Impl>(this)) {
    // FIRMIR_LANGUAGE is an explicit override and wins over the backend.
    const char* forced = std::getenv("FIRMIR_LANGUAGE");
    if (forced && *forced) {
        d->systemLanguage = forced;
    } else {
        d->backend = loadBackend(this);
        d->systemLanguage =
            d->backend ? d->backend->language().toStdString() : envLanguage();
        if (d->backend) {
            d->backendConnection = d->backend->connect<String>(
                this, LocaleBackend::signalLanguageChanged,
                [this](const String& language) {
                    d->onSystemLanguageChanged(language);
                });
        }
    }
    if (d->systemLanguage.empty())
        d->systemLanguage = envLanguage();
    registerProperty<String>("language", &Locale::language,
                             &Locale::setLanguage);
}

Locale::~Locale() = default;

Locale* Locale::instance() {
    static Locale* locale = [] {
        if (auto* existing = Object::singleton<Locale>("Firmir.Core.Locale"))
            return existing;
        auto* created = new Locale;
        Object::registerSingleton("Firmir.Core.Locale", created);
        return created;
    }();
    return locale;
}

String Locale::language() const {
    return String(d->appLanguage.empty() ? d->systemLanguage : d->appLanguage);
}

String Locale::systemLanguage() const { return String(d->systemLanguage); }

bool Locale::followsSystemLanguage() const { return d->appLanguage.empty(); }

LocaleBackend* Locale::backend() const { return d->backend; }

void Locale::setLanguage(String language) {
    const std::string lang = language.toStdString();
    if (lang.empty() || lang == d->appLanguage)
        return;
    d->appLanguage = lang;
    // The application override wins: stop following the backend.
    d->backendConnection.disconnect();
    notifyPropertyChanged("language");
}

void Locale::Impl::onSystemLanguageChanged(const String& language) {
    const std::string lang = language.toStdString();
    if (lang.empty() || lang == systemLanguage)
        return;
    systemLanguage = lang;
    // With an application override the backend was disconnected and this
    // never runs; while following the system the change propagates.
    q->notifyPropertyChanged("language");
}

} // namespace Firmir
