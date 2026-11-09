// firmir-core Translator tests: trId/trIdPlural, translation files, the
// language property and its notifications.

#include "test_framework.hpp"

#include <firmir/translator.hpp>

#include <cstdio>
#include <fstream>

using namespace Firmir;

TEST_CASE(translator_trid_language_switch) {
    auto* tr = Translator::instance();
    const std::string savedLanguage = tr->language().toStdString();

    // Without files every id translates to itself.
    CHECK_EQ(tr->trId(String("weather-title")).toStdString(),
             std::string("weather-title"));

    const char* ruPath = "/tmp/firmir-test-ru.json";
    const char* enPath = "/tmp/firmir-test-en.json";
    {
        std::ofstream ru(ruPath);
        ru << "{\"language\":\"ru\",\"strings\":{\"weather-title\":\"\\u041f\\u043e\\u0433\\u043e\\u0434\\u0430\","
              "\"add-task\":\"\\u0414\\u043e\\u0431\\u0430\\u0432\\u0438\\u0442\\u044c\"}}";
    }
    {
        std::ofstream en(enPath);
        en << "{\"language\":\"en\",\"strings\":{\"weather-title\":\"Weather\","
              "\"add-task\":\"Add task\"}}";
    }

    int languageNotifies = 0;
    // Singleton: keep the stack-captured counter safe via ScopedConnection.
    const ScopedConnection languageConnection(tr->connect<std::string, std::any>(
        tr, Object::signalPropertyChanged,
        [&languageNotifies](const std::string& name, const std::any&) {
            if (name == "language")
                ++languageNotifies;
        }));

    CHECK(tr->addTranslationFile(String(ruPath)));
    CHECK_EQ(languageNotifies, 1); // new strings: refresh bindings
    CHECK(tr->addTranslationFile(String(enPath)));
    CHECK_EQ(languageNotifies, 2);

    // The default language comes from the process locale (usually "en" here);
    // switch to "de" first so the later switch to "en" is a real change.
    tr->setLanguage(String("de"));
    CHECK_EQ(languageNotifies, 3);
    tr->setLanguage(String("en"));
    CHECK_EQ(languageNotifies, 4);
    CHECK_EQ(tr->trId(String("weather-title")).toStdString(),
             std::string("Weather"));
    CHECK_EQ(tr->trId(String("add-task")).toStdString(), std::string("Add task"));

    tr->setLanguage(String("ru"));
    CHECK_EQ(languageNotifies, 5);
    CHECK_EQ(tr->trId(String("weather-title")).toStdString(),
            std::string("\xD0\x9F\xD0\xBE\xD0\xB3\xD0\xBE\xD0\xB4\xD0\xB0")); // "Погода"

    // Same-language set is a no-op (no notification storm).
    tr->setLanguage(String("ru"));
    CHECK_EQ(languageNotifies, 5);

    // Removing a file drops its strings and refreshes.
    CHECK(tr->removeTranslationFile(String(ruPath)));
    CHECK_EQ(languageNotifies, 6);
    CHECK_EQ(tr->trId(String("weather-title")).toStdString(),
             std::string("weather-title"));
    CHECK(!tr->removeTranslationFile(String(ruPath))); // already gone

    // Bad files are rejected without touching the state.
    CHECK(!tr->addTranslationFile(String("/tmp/firmir-test-missing.json")));
    {
        std::ofstream bad("/tmp/firmir-test-bad.json");
        bad << "not json";
    }
    CHECK(!tr->addTranslationFile(String("/tmp/firmir-test-bad.json")));

    CHECK(tr->removeTranslationFile(String(enPath)));
    std::remove(ruPath);
    std::remove(enPath);
    std::remove("/tmp/firmir-test-bad.json");
    tr->setLanguage(String(savedLanguage));
}


TEST_CASE(translator_arguments_and_plurals) {
    auto* tr = Translator::instance();
    const std::string savedLanguage = tr->language().toStdString();

    const char* ruPath = "/tmp/firmir-test-plural-ru.json";
    const char* enPath = "/tmp/firmir-test-plural-en.json";
    {
        std::ofstream ru(ruPath);
        ru << "{\"language\":\"ru\",\"strings\":{"
              "\"tasks-count\":[\"\\u0417\\u0430\\u0434\\u0430\\u0447\\u0430: %n\","
              "\"\\u0417\\u0430\\u0434\\u0430\\u0447\\u0438: %n\","
              "\"\\u0417\\u0430\\u0434\\u0430\\u0447: %n\"],"
              "\"greet\":\"\\u041f\\u0440\\u0438\\u0432\\u0435\\u0442, %1!\"}}";
        // "Задача: %n" / "Задачи: %n" / "Задач: %n", "Привет, %1!"
    }
    {
        std::ofstream en(enPath);
        en << "{\"language\":\"en\",\"strings\":{"
              "\"tasks-count\":[\"%n task\",\"%n tasks\"],"
              "\"greet\":\"Hello, %1! You have %2 messages.\"}}";
    }
    CHECK(tr->addTranslationFile(String(ruPath)));
    CHECK(tr->addTranslationFile(String(enPath)));

    // --- %1..%9 substitution ---------------------------------------------
    tr->setLanguage(String("en"));
    CHECK_EQ(tr->trId(String("greet"), {String("Ann"), String("3")})
                 .toStdString(),
             std::string("Hello, Ann! You have 3 messages."));
    // Missing arguments leave the marker untouched; extras are ignored.
    CHECK_EQ(tr->trId(String("greet"), {String("Ann")}).toStdString(),
             std::string("Hello, Ann! You have %2 messages."));
    // Unknown id: substitution still applies to the id itself.
    CHECK_EQ(tr->trId(String("%1-none"), {String("x")}).toStdString(),
             std::string("x-none"));
    // Russian string, one placeholder.
    tr->setLanguage(String("ru"));
    CHECK_EQ(tr->trId(String("greet"), {String("Bob")}).toStdString(),
             std::string("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, Bob!"));

    // --- %n plural selection: Russian one/few/many ------------------------
    const std::string one = "\xD0\x97\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB0: ";   // "Задача: "
    const std::string few = "\xD0\x97\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x87\xD0\xB8: ";   // "Задачи: "
    const std::string many = "\xD0\x97\xD0\xB0\xD0\xB4\xD0\xB0\xD1\x87: ";          // "Задач: "
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 1).toStdString(), one + "1");
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 2).toStdString(), few + "2");
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 3).toStdString(), few + "3");
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 4).toStdString(), few + "4");
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 5).toStdString(), many + "5");
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 11).toStdString(), many + "11");
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 12).toStdString(), many + "12");
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 21).toStdString(), one + "21");
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 22).toStdString(), few + "22");
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 111).toStdString(),
             many + "111");
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 101).toStdString(),
             one + "101");

    // English one/other; the two-form array clamps nothing.
    tr->setLanguage(String("en"));
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 1).toStdString(),
             std::string("1 task"));
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 2).toStdString(),
             std::string("2 tasks"));
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), 0).toStdString(),
             std::string("0 tasks"));
    CHECK_EQ(tr->trIdPlural(String("tasks-count"), -1).toStdString(),
             std::string("-1 task"));

    // Unknown id: %n still substituted into the id itself.
    CHECK_EQ(tr->trIdPlural(String("%n unknown"), 7).toStdString(),
             std::string("7 unknown"));

    // A language with fewer forms than the rule index clamps to the last.
    tr->setLanguage(String("ru"));
    const char* shortRu = "/tmp/firmir-test-plural-short.json";
    {
        std::ofstream f(shortRu);
        f << "{\"language\":\"ru\",\"strings\":{"
             "\"items\":[\"%n \xD1\x88\xD1\x82.\"]}}"; // "%n шт."
    }
    CHECK(tr->addTranslationFile(String(shortRu)));
    CHECK_EQ(tr->trIdPlural(String("items"), 5).toStdString(),
             std::string("5 \xD1\x88\xD1\x82."));
    CHECK(tr->removeTranslationFile(String(shortRu)));

    CHECK(tr->removeTranslationFile(String(ruPath)));
    CHECK(tr->removeTranslationFile(String(enPath)));
    std::remove(ruPath);
    std::remove(enPath);
    std::remove(shortRu);
    tr->setLanguage(String(savedLanguage));
}
