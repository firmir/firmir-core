#include "test_framework.hpp"

#include <firmir/variant.hpp>

using namespace Firmir;

TEST_CASE(variant_types) {
    Variant v;
    CHECK(!v.isValid());
    CHECK(v.isNull());
    CHECK(v.type() == Variant::Type::Invalid);

    CHECK(Variant(true).type() == Variant::Type::Bool);
    CHECK(Variant(42).type() == Variant::Type::Int);
    CHECK(Variant(42LL).type() == Variant::Type::LongLong);
    CHECK(Variant(1.5).type() == Variant::Type::Double);
    CHECK(Variant("str").type() == Variant::Type::String);
    CHECK(Variant(ByteArray("bytes")).type() == Variant::Type::ByteArray);
}

TEST_CASE(variant_conversions) {
    bool ok = false;

    CHECK_EQ(Variant(42).toInt(&ok), 42);
    CHECK(ok);
    CHECK_EQ(Variant(42).toString(), "42");
    CHECK_EQ(Variant(42).toDouble(), 42.0);
    CHECK(Variant(1).toBool());
    CHECK(!Variant(0).toBool());

    CHECK_EQ(Variant(2.75).toInt(), 2); // truncates like Qt
    CHECK_EQ(Variant(2.75).toDouble(), 2.75);

    CHECK_EQ(Variant("123").toInt(&ok), 123);
    CHECK(ok);
    CHECK_EQ(Variant("abc").toInt(&ok), 0);
    CHECK(!ok);
    CHECK(Variant("true").toBool());
    CHECK(!Variant("false").toBool());

    CHECK_EQ(Variant(true).toString(), "true");
    CHECK_EQ(Variant(false).toInt(), 0);

    CHECK_EQ(Variant(ByteArray("hi")).toString(), "hi");
    CHECK_EQ(Variant(String("hi")).toByteArray(), "hi");

    // Invalid converts to defaults.
    Variant bad;
    CHECK_EQ(bad.toInt(&ok), 0);
    CHECK(!ok);
    CHECK_EQ(bad.toString(), "");
}

TEST_CASE(variant_equality) {
    CHECK(Variant(1) == Variant(1));
    CHECK(Variant(1) != Variant(1LL)); // different types, like Qt
    CHECK(Variant("a") == Variant(String("a")));
    CHECK(Variant("a") != Variant("b"));
    CHECK(Variant() == Variant());
    CHECK(Variant() != Variant(false));

    Variant copy = Variant(7);
    CHECK(copy == Variant(7));
    copy = Variant("seven");
    CHECK(copy != Variant(7));
    CHECK_EQ(copy.toString(), "seven");
}
