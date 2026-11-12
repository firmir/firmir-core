// Firmir::Locale implementation: system language via a pluggable backend
// (firmir-locale-<name>.so) with the process locale as the fallback, plus
// the application override (setLanguage). See doc/core/locale.md.

#include <firmir/locale.hpp>

#include <firmir/detail/dynload.hpp>
#include <firmir/localebackend.hpp>
#include <firmir/log.hpp>

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace Firmir {

namespace {

const LoggingCategory& lcLocale() {
    static const LoggingCategory category("locale");
    return category;
}

/// Directories searched for firmir-locale-<name> plugins (.so/.dll).
std::vector<std::string> pluginDirs() {
    std::vector<std::string> dirs = detail::pluginSearchDirs(
        "FIRMIR_CORE_PLUGIN_PATH",
        {"../plugins/core", "../../plugins/core", "../../../plugins/core",
         "plugins/core"});
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

/// Tries to load firmir-locale-<name> (.so/.dll) from every search directory.
LocaleBackend* loadBackendNamed(const std::string& name, Object* host) {
    const std::string file =
        "firmir-locale-" + name + detail::sharedLibrarySuffix();
    for (const std::string& dir : pluginDirs()) {
        const std::string path = dir + "/" + file;
        void* handle = detail::libraryOpen(path);
        if (!handle)
            continue;
        auto entry = reinterpret_cast<LocalePluginEntry>(
            detail::librarySymbol(handle, FIRMIR_LOCALE_PLUGIN_ENTRY));
        const LocalePlugin* plugin = entry ? entry() : nullptr;
        if (!plugin || plugin->abiVersion != FIRMIR_LOCALE_PLUGIN_ABI) {
            cLogWarning(lcLocale()) << "bad plugin ABI in" << path;
            detail::libraryClose(handle);
            continue;
        }
        if (plugin->available && !plugin->available()) {
            cLogInfo(lcLocale()) << "locale backend unavailable:" << name;
            detail::libraryClose(handle);
            continue;
        }
        if (!plugin->create) {
            detail::libraryClose(handle);
            continue;
        }
        cLogInfo(lcLocale()) << "loaded locale backend" << path;
        return plugin->create(host);
    }
    return nullptr;
}

/// Picks a backend: FIRMIR_LOCALE_BACKEND pins a name; otherwise the first
/// firmir-locale-* plugin found in the search directories.
LocaleBackend* loadBackend(Object* host) {
    const char* pinned = std::getenv("FIRMIR_LOCALE_BACKEND");
    if (pinned && *pinned)
        return loadBackendNamed(pinned, host);
    const std::string suffix = detail::sharedLibrarySuffix();
    std::error_code ec;
    for (const std::string& dir : pluginDirs()) {
        for (const auto& entry :
             std::filesystem::directory_iterator(dir, ec)) {
            const std::string file = entry.path().filename().string();
            if (file.rfind("firmir-locale-", 0) != 0 ||
                file.size() <= suffix.size() + 14 ||
                file.compare(file.size() - suffix.size(), suffix.size(),
                             suffix) != 0)
                continue;
            // Strip the prefix and the library suffix.
            const std::string name =
                file.substr(14, file.size() - 14 - suffix.size());
            if (LocaleBackend* backend = loadBackendNamed(name, host))
                return backend;
        }
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
