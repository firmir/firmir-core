#include "test_framework.hpp"

#include <firmir/string.hpp>

using namespace Firmir;

TEST_CASE(string_basics) {
    String s;
    CHECK(s.isEmpty());
    CHECK_EQ(s.size(), 0);

    s = "hello";
    CHECK_EQ(s.size(), 5);
    CHECK_EQ(s.at(0), 'h');
    CHECK_EQ(s[4], 'o');

    s.append(" world");
    CHECK_EQ(s, "hello world");
    s.prepend("say: ");
    CHECK_EQ(s, "say: hello world");
    s += '!';
    CHECK_EQ(s, "say: hello world!");

    String t = s + "?";
    CHECK_EQ(t, "say: hello world!?");
    CHECK_EQ(s, "say: hello world!"); // unchanged

    String repeated(3, 'x');
    CHECK_EQ(repeated, "xxx");

    CHECK(String(nullptr).isEmpty());
}

TEST_CASE(string_search) {
    const String s = "The quick brown fox";

    CHECK_EQ(s.indexOf("quick"), 4);
    CHECK_EQ(s.indexOf("missing"), -1);
    CHECK_EQ(s.indexOf("brown"), 10);
    CHECK_EQ(s.indexOf("o"), 12);
    CHECK_EQ(s.indexOf("o", 13), 17);
    CHECK_EQ(s.lastIndexOf("o"), 17);

    CHECK(s.contains("brown"));
    CHECK(!s.contains("BROWN"));
    CHECK(s.contains("BROWN", String::CaseSensitivity::Insensitive));

    CHECK(s.startsWith("The"));
    CHECK(!s.startsWith("the"));
    CHECK(s.startsWith("the", String::CaseSensitivity::Insensitive));
    CHECK(s.endsWith("fox"));
    CHECK(!s.endsWith("Fox"));
    CHECK(s.endsWith("Fox", String::CaseSensitivity::Insensitive));
}

TEST_CASE(string_transform) {
    CHECK_EQ(String("  padded \t\n").trimmed(), "padded");
    CHECK_EQ(String("MiXeD").toLower(), "mixed");
    CHECK_EQ(String("MiXeD").toUpper(), "MIXED");

    const String s = "abcdefgh";
    CHECK_EQ(s.mid(2), "cdefgh");
    CHECK_EQ(s.mid(2, 3), "cde");
    CHECK_EQ(s.mid(100), "");
    CHECK_EQ(s.left(3), "abc");
    CHECK_EQ(s.right(2), "gh");

    String r = "a-b-c";
    r.replace("-", "+");
    CHECK_EQ(r, "a+b+c");
    r.remove("+");
    CHECK_EQ(r, "abc");

    const auto parts = String("a,b,,c").split(',');
    CHECK_EQ(parts.size(), 4u);
    CHECK_EQ(parts[1], "b");
    CHECK_EQ(parts[2], "");

    const auto chunks = String("x::y::z").split("::");
    CHECK_EQ(chunks.size(), 3u);
    CHECK_EQ(chunks[2], "z");
}

TEST_CASE(string_numbers) {
    CHECK_EQ(String::number(42), "42");
    CHECK_EQ(String::number(-7LL), "-7");
    CHECK_EQ(String::number(3.5), "3.5");

    bool ok = false;
    CHECK_EQ(String("123").toInt(&ok), 123);
    CHECK(ok);
    CHECK_EQ(String("12x").toInt(&ok), 0);
    CHECK(!ok);
    CHECK_EQ(String("ff").toInt(&ok, 16), 255);
    CHECK(ok);

    CHECK_EQ(String("2.5").toDouble(&ok), 2.5);
    CHECK(ok);
    CHECK_EQ(String("").toDouble(&ok), 0.0);
    CHECK(!ok);

    CHECK_EQ(String("%1 of %2 done").arg(3).arg(10), "3 of 10 done");
    CHECK_EQ(String("%1/%2").arg(3).arg(10), "3/10");
}
