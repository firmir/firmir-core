#include "test_framework.hpp"

#include <firmir/regexp.hpp>

#include <string>
#include <vector>

using namespace Firmir;

TEST_CASE(regexp_validity) {
    RegExp empty;
    CHECK(empty.isValid());
    CHECK(empty.isEmpty());
    CHECK_EQ(empty.errorString(), "");

    RegExp good("(\\d+)-(\\d+)");
    CHECK(good.isValid());
    CHECK_EQ(good.captureCount(), 2);

    RegExp bad("(unclosed");
    CHECK(!bad.isValid());
    CHECK(!bad.errorString().empty());
    CHECK_EQ(bad.indexIn("anything"), -1);
    CHECK(!bad.exactMatch("anything"));
    CHECK_EQ(bad.captureCount(), 0);
}

TEST_CASE(regexp_exact_match) {
    RegExp re("\\d+");
    CHECK(re.exactMatch("12345"));
    CHECK_EQ(re.cap(0), "12345");
    CHECK_EQ(re.matchedLength(), 5);

    CHECK(!re.exactMatch("123a5"));
    CHECK_EQ(re.matchedLength(), -1); // match state cleared on failure
    CHECK(!re.exactMatch(""));

    CHECK(re.exactMatch("777"));
    CHECK_EQ(re.cap(0), "777");
    CHECK_EQ(re.matchedLength(), 3);
}

TEST_CASE(regexp_index_in_and_captures) {
    RegExp re("(\\w+)@(\\w+)\\.(\\w+)");
    const std::string text = "mail me at user@example.com please";

    const int idx = re.indexIn(text);
    CHECK_EQ(idx, 11);
    CHECK_EQ(re.cap(0), "user@example.com");
    CHECK_EQ(re.cap(1), "user");
    CHECK_EQ(re.cap(2), "example");
    CHECK_EQ(re.cap(3), "com");
    CHECK_EQ(re.cap(4), ""); // out of range
    CHECK_EQ(re.pos(0), 11);
    CHECK_EQ(re.pos(2), 16);
    CHECK_EQ(re.pos(9), -1);
    CHECK_EQ(re.matchedLength(), 16);

    const auto texts = re.capturedTexts();
    CHECK_EQ(texts.size(), 4u);
    CHECK_EQ(texts[3], "com");

    // offset 12: \w+ still matches starting at 's' of "ser@example.com"
    CHECK_EQ(re.indexIn(text, 12), 12);
    CHECK_EQ(re.cap(0), "ser@example.com");

    // no match at all
    CHECK_EQ(re.indexIn("no mail here"), -1);
    CHECK_EQ(re.matchedLength(), -1); // state cleared on failure

    // offset right at the match still finds it
    CHECK_EQ(re.indexIn(text, 11), 11);
}

TEST_CASE(regexp_last_index_in) {
    RegExp re("\\d+");
    const std::string text = "a1 b22 c333";
    // indices: a=0 '1'=1 b=3 '2'=4 '2'=5 c=7 '3'=8..10
    CHECK_EQ(re.lastIndexIn(text), 8);
    CHECK_EQ(re.cap(0), "333");

    CHECK_EQ(re.lastIndexIn(text, 5), 4); // match starting at 4 ("22")
    CHECK_EQ(re.cap(0), "22");

    CHECK_EQ(re.lastIndexIn(text, 1), 1);
    CHECK_EQ(re.cap(0), "1");

    CHECK_EQ(re.lastIndexIn(text, 0), -1); // no match starting at pos 0
}

TEST_CASE(regexp_case_sensitivity) {
    RegExp re("hello");
    CHECK_EQ(re.indexIn("say HELLO"), -1);

    re.setCaseSensitivity(RegExp::CaseSensitivity::Insensitive);
    CHECK(re.caseSensitivity() == RegExp::CaseSensitivity::Insensitive);
    CHECK_EQ(re.indexIn("say HELLO"), 4);
    CHECK_EQ(re.cap(0), "HELLO");

    RegExp ins("hello", RegExp::CaseSensitivity::Insensitive);
    CHECK(ins.exactMatch("HeLLo"));
}

TEST_CASE(regexp_minimal) {
    RegExp re("a.*b");
    CHECK(!re.isMinimal());
    CHECK_EQ(re.indexIn("aXbYb"), 0);
    CHECK_EQ(re.cap(0), "aXbYb"); // greedy

    re.setMinimal(true);
    CHECK(re.isMinimal());
    CHECK_EQ(re.indexIn("aXbYb"), 0);
    CHECK_EQ(re.cap(0), "aXb"); // lazy

    // {n,m} quantifiers are converted too.
    RegExp braces("a{1,3}");
    braces.setMinimal(true);
    CHECK_EQ(braces.indexIn("aaa"), 0);
    CHECK_EQ(braces.cap(0), "a");

    // Already-lazy quantifiers are not double-marked.
    RegExp lazy("a.*?b");
    lazy.setMinimal(true);
    CHECK_EQ(lazy.indexIn("aXbYb"), 0);
    CHECK_EQ(lazy.cap(0), "aXb");
}

TEST_CASE(regexp_escape) {
    const std::string literal = "a.b*c(d)[e]{f}+$^|\\?";
    RegExp re(RegExp::escape(literal));
    CHECK(re.isValid());
    CHECK(re.exactMatch(literal));
    CHECK(!re.exactMatch("aXbYc"));
}

TEST_CASE(regexp_copy_and_equality) {
    RegExp a("(\\d+)", RegExp::CaseSensitivity::Insensitive);
    RegExp b = a; // copy
    CHECK(a == b);
    CHECK(!(a != b));
    CHECK_EQ(b.captureCount(), 1);
    CHECK_EQ(b.indexIn("n42"), 1);

    b.setPattern("x+");
    CHECK(a != b);

    RegExp c(std::move(b)); // move
    CHECK_EQ(c.pattern(), "x+");

    RegExp d;
    c.setMinimal(true);
    d = c;
    CHECK(c == d);
    CHECK(d.isMinimal());
}

TEST_CASE(regexp_optional_group) {
    RegExp re("(a)(b)?(c)");
    CHECK_EQ(re.indexIn("ac"), 0);
    CHECK_EQ(re.cap(1), "a");
    CHECK_EQ(re.cap(2), "");   // group did not participate
    CHECK_EQ(re.pos(2), -1);
    CHECK_EQ(re.cap(3), "c");
    CHECK_EQ(re.capturedTexts().size(), 4u);
}
