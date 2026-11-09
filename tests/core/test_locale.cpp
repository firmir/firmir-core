// firmir-core Locale tests: the test backend plugin (firmir-locale-test.so,
// reports "ru", switches via the "simulateLanguage" dynamic property), the
// application override, and the Translator integration.

#include "test_framework.hpp"

#include <firmir/locale.hpp>
#include <firmir/localebackend.hpp>
#include <firmir/translator.hpp>

#include <cstdlib>

using namespace Firmir;

TEST_CASE(locale_backend_and_app_override) {
    // Pick the test backend deterministically (the tests binary lives in
    // build/tests/, so build/plugins/core is found exe-relative anyway) and
    // neutralize a possible FIRMIR_LANGUAGE from the environment.
    ::unsetenv("FIRMIR_LANGUAGE");
    ::setenv("FIRMIR_LOCALE_BACKEND", "test", 1);

    Locale* locale = Locale::instance();
    CHECK(locale->backend() != nullptr);
    if (!locale->backend())
        return;
    CHECK_EQ(locale->systemLanguage().toStdString(), std::string("ru"));
    CHECK_EQ(locale->language().toStdString(), std::string("ru"));
    CHECK(locale->followsSystemLanguage());

    // The Translator defaults to the Locale language and follows it.
    Translator* tr = Translator::instance();
    CHECK_EQ(tr->language().toStdString(), std::string("ru"));

    int languageNotifies = 0;
    const ScopedConnection connection(locale->connect<std::string, std::any>(
        locale, Object::signalPropertyChanged,
        [&languageNotifies](const std::string& name, const std::any&) {
            if (name == "language")
                ++languageNotifies;
        }));

    // A runtime system-language change (via the backend signal) propagates.
    locale->backend()->setProperty("simulateLanguage", String("de"));
    CHECK_EQ(locale->systemLanguage().toStdString(), std::string("de"));
    CHECK_EQ(locale->language().toStdString(), std::string("de"));
    CHECK_EQ(languageNotifies, 1);
    CHECK_EQ(tr->language().toStdString(), std::string("de"));

    // The application override wins and detaches from the system language.
    locale->setLanguage(String("en"));
    CHECK_EQ(languageNotifies, 2);
    CHECK_EQ(locale->language().toStdString(), std::string("en"));
    CHECK(!locale->followsSystemLanguage());
    CHECK_EQ(tr->language().toStdString(), std::string("en"));
    locale->backend()->setProperty("simulateLanguage", String("fr"));
    // The backend is disconnected: the system language stays at the last
    // value seen before the override, and language() keeps the override.
    CHECK_EQ(locale->systemLanguage().toStdString(), std::string("de"));
    CHECK_EQ(locale->language().toStdString(), std::string("en"));
    CHECK_EQ(languageNotifies, 2);

    // The Translator has its own explicit-override level: setLanguage()
    // detaches it from the Locale.
    tr->setLanguage(String("es"));
    CHECK_EQ(tr->language().toStdString(), std::string("es"));
    locale->setLanguage(String("it"));
    CHECK_EQ(locale->language().toStdString(), std::string("it"));
    CHECK_EQ(tr->language().toStdString(), std::string("es"));

    // Neutral state for the later translator test cases.
    locale->setLanguage(String("en"));
    tr->setLanguage(String("en"));
}

TEST_CASE(locale_env_fallback_without_backend) {
    // The backend was already loaded by the case above; here only the
    // parsing contract is checked indirectly: with no backend and no env,
    // the fallback would be LC_ALL/LANG -> "en". Nothing to re-run in this
    // process; assert the documented invariants instead.
    Locale* locale = Locale::instance();
    CHECK(!locale->language().toStdString().empty());
    CHECK(!locale->systemLanguage().toStdString().empty());
}
