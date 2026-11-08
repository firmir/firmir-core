#include "test_framework.hpp"

#include <firmir/settings.hpp>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

using namespace Firmir;

namespace {

/// Creates a unique temporary INI file path (the file itself is removed
/// so the test starts from a clean state).
String tempSettingsPath(const char* tag) {
    String path = "/tmp/firmir-settings-" + String(tag) + "-" +
                  String::number(static_cast<long long>(::getpid())) + ".ini";
    std::remove(path.toStdString().c_str());
    return path;
}

} // namespace

TEST_CASE(settings_basics) {
    const String path = tempSettingsPath("basics");
    {
        Settings s(path);
        CHECK(s.value("missing").isNull());
        CHECK_EQ(s.value("missing", 42).toInt(), 42);
        CHECK(!s.contains("name"));

        s.setValue("name", "firmir");
        s.setValue("count", 7);
        CHECK(s.contains("name"));
        CHECK_EQ(s.value("name").toString(), "firmir");
        CHECK_EQ(s.value("count").toString(), "7");
        CHECK(s.sync());
    }
    // Reload from disk.
    {
        Settings s(path);
        CHECK_EQ(s.value("name").toString(), "firmir");
        CHECK_EQ(s.value("count").toString(), "7");
    }
    std::remove(path.toStdString().c_str());
}

TEST_CASE(settings_groups) {
    const String path = tempSettingsPath("groups");
    {
        Settings s(path);
        s.setValue("general/key", "top");
        s.beginGroup("network");
        CHECK_EQ(s.group(), "network");
        s.setValue("host", "example.com");
        s.setValue("port", 8080);
        s.beginGroup("tls");
        s.setValue("enabled", true);
        s.endGroup();
        CHECK_EQ(s.group(), "network");
        s.endGroup();

        CHECK_EQ(s.value("network/host").toString(), "example.com");
        CHECK_EQ(s.value("network/tls/enabled").toString(), "true");

        const auto groups = s.childGroups();
        CHECK_EQ(groups.size(), 2u);
        CHECK_EQ(groups[0], "general");
        CHECK_EQ(groups[1], "network");

        s.beginGroup("network");
        const auto keys = s.childKeys();
        CHECK_EQ(keys.size(), 2u);
        CHECK_EQ(keys[0], "host");
        CHECK_EQ(keys[1], "port");
        CHECK_EQ(s.childGroups().size(), 1u);
        CHECK_EQ(s.childGroups()[0], "tls");
        s.endGroup();
    }
    // Reload and verify persistence of groups.
    {
        Settings s(path);
        CHECK_EQ(s.value("network/host").toString(), "example.com");
        CHECK_EQ(s.value("network/tls/enabled").toString(), "true");
    }
    std::remove(path.toStdString().c_str());
}

TEST_CASE(settings_remove_clear) {
    const String path = tempSettingsPath("remove");
    Settings s(path);
    s.setValue("a", "1");
    s.setValue("grp/b", "2");
    s.setValue("grp/c", "3");

    s.remove("a");
    CHECK(!s.contains("a"));

    s.remove("grp"); // removes a whole group
    CHECK(!s.contains("grp/b"));
    CHECK(!s.contains("grp/c"));

    s.setValue("x", "y");
    s.clear();
    CHECK(!s.contains("x"));
    std::remove(path.toStdString().c_str());
}

TEST_CASE(settings_special_chars) {
    const String path = tempSettingsPath("escape");
    {
        Settings s(path);
        s.setValue("multiline", "line1\nline2\r\nback\\slash");
        CHECK(s.sync());
    }
    {
        Settings s(path);
        CHECK_EQ(s.value("multiline").toString(),
                 "line1\nline2\r\nback\\slash");
    }
    std::remove(path.toStdString().c_str());
}

TEST_CASE(settings_writable) {
    const String path = tempSettingsPath("writable");
    Settings s(path);
    CHECK(s.isWritable());
    CHECK_EQ(s.fileName(), path);

    Settings ro("/proc/firmir/definitely/not-writable.ini");
    CHECK(!ro.isWritable());
    ro.setValue("k", "v"); // a no-op sync() of clean state would succeed
    CHECK(!ro.sync());
}

TEST_CASE(settings_org_app_path) {
    Settings s("FirmirOrg", "FirmirApp");
    const String fileName = s.fileName();
    CHECK(fileName.contains("FirmirOrg/FirmirApp.ini"));
}

TEST_CASE(settings_end_group_without_begin) {
    const String path = tempSettingsPath("nogroup");
    Settings s(path);
    s.endGroup(); // must not crash
    CHECK_EQ(s.group(), "");
    std::remove(path.toStdString().c_str());
}
