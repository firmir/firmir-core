#include "test_framework.hpp"

#include <firmir/bytearray.hpp>

using namespace Firmir;

TEST_CASE(byte_array_basics) {
    ByteArray b;
    CHECK(b.isEmpty());

    b = ByteArray("hello");
    CHECK_EQ(b.size(), 5);
    CHECK_EQ(b.at(1), 'e');

    b.append(" world");
    CHECK_EQ(b, "hello world");
    b.prepend(">> ");
    CHECK_EQ(b, ">> hello world");
    b += '!';
    CHECK_EQ(b, ">> hello world!");

    ByteArray raw("a\0b", 3); // explicit size keeps the embedded NUL
    CHECK_EQ(raw.size(), 3);
    CHECK_EQ(raw.at(1), '\0');

    ByteArray filled(4, 'z');
    CHECK_EQ(filled, "zzzz");
}

TEST_CASE(byte_array_search_and_slice) {
    const ByteArray b = "one two one";
    CHECK_EQ(b.indexOf("two"), 4);
    CHECK_EQ(b.indexOf("one", 1), 8);
    CHECK(b.contains("two"));
    CHECK(b.startsWith("one"));
    CHECK(b.endsWith("one"));
    CHECK(!b.startsWith("two"));

    CHECK_EQ(b.mid(4, 3), "two");
    CHECK_EQ(b.left(3), "one");
    CHECK_EQ(b.right(3), "one");
    CHECK_EQ(ByteArray("  x ").trimmed(), "x");

    ByteArray r = "abcdef";
    r.remove(1, 3); // removes "bcd"
    CHECK_EQ(r, "aef");
}

TEST_CASE(byte_array_numbers) {
    bool ok = false;
    CHECK_EQ(ByteArray("42").toInt(&ok), 42);
    CHECK(ok);
    CHECK_EQ(ByteArray("4x").toInt(&ok), 0);
    CHECK(!ok);
    CHECK_EQ(ByteArray("1.5").toDouble(&ok), 1.5);
    CHECK(ok);
}

TEST_CASE(byte_array_base64) {
    // RFC 4648 test vectors
    CHECK_EQ(ByteArray("").toBase64(), "");
    CHECK_EQ(ByteArray("f").toBase64(), "Zg==");
    CHECK_EQ(ByteArray("fo").toBase64(), "Zm8=");
    CHECK_EQ(ByteArray("foo").toBase64(), "Zm9v");
    CHECK_EQ(ByteArray("foob").toBase64(), "Zm9vYg==");
    CHECK_EQ(ByteArray("fooba").toBase64(), "Zm9vYmE=");
    CHECK_EQ(ByteArray("foobar").toBase64(), "Zm9vYmFy");

    CHECK_EQ(ByteArray::fromBase64("Zm9vYmFy"), "foobar");
    CHECK_EQ(ByteArray::fromBase64("Zg=="), "f");
    CHECK_EQ(ByteArray::fromBase64("Zm9v\nYmFy"), "foobar"); // noise skipped

    // Round-trip over binary data with NUL bytes.
    const ByteArray binary("\x01\x02\x00\xff\xfe", 5);
    CHECK_EQ(ByteArray::fromBase64(binary.toBase64()), binary);
}

TEST_CASE(byte_array_hex) {
    CHECK_EQ(ByteArray("\xde\xad\xbe\xef", 4).toHex(), "deadbeef");
    CHECK_EQ(ByteArray::fromHex("deadbeef"), ByteArray("\xde\xad\xbe\xef", 4));
    CHECK_EQ(ByteArray("\x01\x02", 2).toHex(':'), "01:02");
    CHECK_EQ(ByteArray::fromHex("01:02"), ByteArray("\x01\x02", 2));
    CHECK_EQ(ByteArray("").toHex(), "");
}
