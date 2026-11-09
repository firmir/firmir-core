// Test Locale backend plugin (firmir-locale-test.so).
//
// Reports the system language as Russian. Setting the dynamic property
// "simulateLanguage" (String) changes the reported language and emits
// signalLanguageChanged — that is how tests exercise the runtime language
// switch without a real system underneath.

#include <firmir/localebackend.hpp>

#include <any>

namespace Firmir {
namespace {

class TestLocaleBackend : public LocaleBackend {
public:
    explicit TestLocaleBackend(Object* host) : LocaleBackend(host) {
        connect<std::string, std::any>(
            this, Object::signalPropertyChanged,
            [this](const std::string& name, const std::any& value) {
                if (name != "simulateLanguage")
                    return;
                const auto* lang = std::any_cast<String>(&value);
                if (!lang)
                    return;
                const std::string next = lang->toStdString();
                if (next.empty() || next == m_language)
                    return;
                m_language = next;
                emitSignal<String>(signalLanguageChanged, String(m_language));
            });
    }

    String language() const override { return String(m_language); }

private:
    std::string m_language = "ru";
};

/// Plugin entry (firmir-locale-test.so).
const LocalePlugin* testPlugin() {
    static const LocalePlugin plugin = {
        FIRMIR_LOCALE_PLUGIN_ABI, "test",
        []() -> bool { return true; },
        [](Object* host) -> LocaleBackend* {
            return new TestLocaleBackend(host);
        }};
    return &plugin;
}

} // namespace
} // namespace Firmir

extern "C" const Firmir::LocalePlugin* firmir_locale_plugin() {
    return Firmir::testPlugin();
}
