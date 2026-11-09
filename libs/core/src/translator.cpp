#include <firmir/translator.hpp>

#include <firmir/json.hpp>
#include <firmir/locale.hpp>
#include <firmir/log.hpp>

#include <cstdlib>
#include <fstream>
#include <sstream>

namespace Firmir {

namespace {

/// Index of the plural form for @p n under @p language's rules
/// (CLDR-style, reduced to the languages Firmir ships examples for).
/// Russian/Ukrainian/Belarusian: one/few/many; everything else: one/other.
int pluralFormIndex(const std::string& language, long long n) {
    const unsigned long long a =
        n < 0 ? ~static_cast<unsigned long long>(n) + 1
              : static_cast<unsigned long long>(n);
    if (language == "ru" || language == "uk" || language == "be") {
        const unsigned mod10 = a % 10;
        const unsigned mod100 = a % 100;
        if (mod10 == 1 && mod100 != 11)
            return 0; // one
        if (mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14))
            return 1; // few
        return 2;     // many
    }
    return a == 1 ? 0 : 1; // one/other (English and the default)
}

/// Replaces %1..%9 (from @p args) and %n (when @p n is set) in @p text.
std::string substitute(std::string text, std::initializer_list<String> args,
                       const long long* n) {
    int i = 1;
    for (const String& arg : args) {
        if (i > 9)
            break;
        const std::string marker = "%" + std::to_string(i++);
        std::string::size_type pos = 0;
        while ((pos = text.find(marker, pos)) != std::string::npos) {
            text.replace(pos, marker.size(), arg.toStdString());
            pos += arg.size();
        }
    }
    if (n) {
        const std::string number = std::to_string(*n);
        std::string::size_type pos = 0;
        while ((pos = text.find("%n", pos)) != std::string::npos) {
            text.replace(pos, 2, number);
            pos += number.size();
        }
    }
    return text;
}

} // namespace

Translator::Translator(Object* parent)
    : Object(parent), m_language(Locale::instance()->language().toStdString()) {
    // Follow the Locale (system language, or the application override set on
    // it) until the application sets the Translator language directly.
    m_localeConnection = Locale::instance()->connect<std::string, std::any>(
        this, Object::signalPropertyChanged,
        [this](const std::string& name, const std::any& value) {
            if (name != "language")
                return;
            const auto* lang = std::any_cast<String>(&value);
            const std::string next = lang ? lang->toStdString() : std::string();
            if (next.empty() || next == m_language)
                return;
            m_language = next;
            notifyLanguageChanged();
        });
    registerProperty<String>("language", &Translator::language,
                             &Translator::setLanguage);
}

Translator* Translator::instance() {
    static Translator* translator = [] {
        if (auto* existing = Object::singleton<Translator>("Firmir.Core.Translator"))
            return existing;
        auto* created = new Translator;
        Object::registerSingleton("Firmir.Core.Translator", created);
        return created;
    }();
    return translator;
}

const std::vector<std::string>*
Translator::findForms(const std::string& key) const {
    // Later-added files win over earlier ones for the same id.
    for (auto it = m_files.rbegin(); it != m_files.rend(); ++it) {
        if (it->second.language != m_language)
            continue;
        const auto strIt = it->second.strings.find(key);
        if (strIt != it->second.strings.end() && !strIt->second.empty() &&
            !strIt->second.front().empty())
            return &strIt->second;
    }
    return nullptr;
}

String Translator::trId(const String& id) const {
    if (const auto* forms = findForms(id.toStdString()))
        return String((*forms)[0]);
    return id; // untranslated ids stay readable
}

String Translator::trId(const String& id,
                        std::initializer_list<String> args) const {
    if (const auto* forms = findForms(id.toStdString()))
        return String(substitute((*forms)[0], args, nullptr));
    return String(substitute(id.toStdString(), args, nullptr));
}

String Translator::trIdPlural(const String& id, long long n) const {
    if (const auto* forms = findForms(id.toStdString())) {
        int form = pluralFormIndex(m_language, n);
        if (form >= static_cast<int>(forms->size()))
            form = static_cast<int>(forms->size()) - 1; // clamp: fewer forms
        return String(substitute((*forms)[form], {}, &n));
    }
    return String(substitute(id.toStdString(), {}, &n));
}

void Translator::setLanguage(String language) {
    const std::string lang = language.toStdString();
    if (lang.empty() || lang == m_language)
        return;
    // Explicit application choice: stop following the Locale (mirrors how
    // Locale::setLanguage stops following the system backend).
    m_localeConnection.disconnect();
    m_language = lang;
    notifyLanguageChanged();
}

bool Translator::addTranslationFile(String path) {
    std::ifstream in(path.toStdString());
    if (!in) {
        logWarning() << "Translator: cannot open" << path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string text = buffer.str();
    JsonDocument::ParseError parseError;
    const JsonDocument doc = JsonDocument::fromJson(
        ByteArray(text.data(), static_cast<int>(text.size())), &parseError);
    if (parseError.error != JsonDocument::ParseError::NoError ||
        !doc.isObject()) {
        logWarning() << "Translator: bad JSON in" << path;
        return false;
    }
    const JsonObject root = doc.object();
    const std::string language =
        root.value(String("language")).toString().toStdString();
    if (language.empty()) {
        logWarning() << "Translator: no \"language\" tag in" << path;
        return false;
    }
    const JsonObject strings = root.value(String("strings")).toObject();
    auto& file = m_files[path.toStdString()];
    file.language = language;
    file.strings.clear();
    for (const auto& key : strings.keys()) {
        const JsonValue value = strings.value(key);
        std::vector<std::string> forms;
        if (value.isArray()) {
            // Plural forms: ["%n задача", "%n задачи", "%n задач"].
            const JsonArray array = value.toArray();
            for (int i = 0; i < array.size(); ++i)
                if (array.at(i).isString())
                    forms.push_back(array.at(i).toString().toStdString());
        } else {
            forms.push_back(value.toString().toStdString());
        }
        if (!forms.empty())
            file.strings[key.toStdString()] = std::move(forms);
    }
    logInfo() << "Translator: loaded" << path << "(language:" << language.c_str()
              << ")";
    // New strings may change trId() results: refresh all bindings.
    notifyLanguageChanged();
    return true;
}

bool Translator::removeTranslationFile(String path) {
    if (!m_files.erase(path.toStdString()))
        return false;
    notifyLanguageChanged();
    return true;
}

std::vector<String> Translator::translationFiles() const {
    std::vector<String> out;
    out.reserve(m_files.size());
    for (const auto& entry : m_files)
        out.push_back(String(entry.first));
    return out;
}

void Translator::notifyLanguageChanged() {
    notifyPropertyChanged("language");
}

} // namespace Firmir
