#include "test_framework.hpp"

#include <firmir/object.hpp>
#include <firmir/thread.hpp>

#include <atomic>
#include <stdexcept>
#include <string>

using namespace Firmir;

namespace {

struct Tracked : Object {
    explicit Tracked(std::atomic<int>* counter, Object* parent = nullptr)
        : Object(parent), dtorCounter(counter) {}
    ~Tracked() override {
        if (dtorCounter)
            dtorCounter->fetch_add(1);
    }
    std::atomic<int>* dtorCounter;
};

} // namespace

TEST_CASE(object_ownership_tree) {
    std::atomic<int> destroyed{0};
    auto* parentObj = new Tracked(&destroyed);
    auto* child = new Tracked(&destroyed, parentObj);
    auto* grandChild = new Tracked(&destroyed, child);

    CHECK_EQ(parentObj->children().size(), 1u);
    CHECK_EQ(child->parent(), parentObj);
    CHECK_EQ(grandChild->parent(), child);

    delete parentObj;
    CHECK_EQ(destroyed.load(), 3);
}

TEST_CASE(object_reparenting) {
    std::atomic<int> destroyed{0};
    Object a;
    Object b;
    Tracked moving(&destroyed);
    moving.setParent(&a);
    CHECK_EQ(moving.parent(), &a);
    CHECK_EQ(a.children().size(), 1u);
    moving.setParent(&b);
    CHECK_EQ(a.children().size(), 0u);
    CHECK_EQ(b.children().size(), 1u);
    moving.setParent(nullptr);
    CHECK_EQ(b.children().size(), 0u);
    CHECK_EQ(moving.parent(), nullptr);
}

TEST_CASE(object_setparent_cross_thread_fails) {
    Thread other;
    Object hosted;
    hosted.moveToThread(&other);
    Object local;
    CHECK_THROWS_AS(local.setParent(&hosted), std::logic_error);
    hosted.moveToThread(Thread::mainThread());
}

TEST_CASE(object_name_and_find) {
    Object root;
    root.setName("root");
    Object child1(&root);
    child1.setName("child1");
    Object child2(&root);
    child2.setName("child2");
    Object nested(&child2);
    nested.setName("deep");

    CHECK(root.findChild("child1") == &child1);
    CHECK(root.findChild("deep") == &nested);
    CHECK(root.findChild("missing") == nullptr);
    CHECK_EQ(root.findChildren().size(), 3u);
    CHECK_EQ(root.name(), "root");
}

TEST_CASE(object_properties) {
    Object obj;
    CHECK(!obj.hasProperty("key"));
    obj.setProperty("key", 42);
    CHECK(obj.hasProperty("key"));
    CHECK_EQ(std::any_cast<int>(obj.property("key")), 42);
    obj.setProperty("key", std::string("text"));
    CHECK_EQ(std::any_cast<std::string>(obj.property("key")), "text");
    CHECK(!obj.property("absent").has_value());
}

TEST_CASE(object_property_changed_dynamic) {
    Object obj;
    int calls = 0;
    std::string lastName;
    obj.connect<std::string, std::any>(
        &obj, Object::signalPropertyChanged,
        [&](std::string name, std::any value) {
            ++calls;
            lastName = name;
            CHECK_EQ(std::any_cast<int>(value), 7);
        },
        ConnectionMode::Direct);
    obj.setProperty("level", 7);
    CHECK_EQ(calls, 1);
    CHECK_EQ(lastName, "level");
    // Dynamic properties notify on every set (no equality available).
    obj.setProperty("level", 7);
    CHECK_EQ(calls, 2);
}

TEST_CASE(object_registered_property) {
    class Gauge : public Object {
    public:
        Gauge() { registerProperty("level", &Gauge::level, &Gauge::setLevel); }
        int level() const { return m_level; }
        void setLevel(int level) {
            if (m_level == level)
                return;
            m_level = level;
            notifyPropertyChanged("level");
        }

    private:
        int m_level = 0;
    };

    Gauge gauge;
    CHECK(gauge.hasProperty("level"));
    CHECK_EQ(gauge.level(), 0);

    int calls = 0;
    std::string lastName;
    int lastValue = -1;
    gauge.connect<std::string, std::any>(
        &gauge, Object::signalPropertyChanged,
        [&](std::string name, std::any value) {
            ++calls;
            lastName = name;
            lastValue = std::any_cast<int>(value);
        },
        ConnectionMode::Direct);

    gauge.setProperty("level", 42);
    CHECK_EQ(gauge.level(), 42);
    CHECK_EQ(std::any_cast<int>(gauge.getProperty("level")), 42);
    CHECK_EQ(calls, 1);
    CHECK_EQ(lastName, "level");
    CHECK_EQ(lastValue, 42);

    // No change -> no notification.
    gauge.setProperty("level", 42);
    CHECK_EQ(calls, 1);

    // Lenient conversion: double -> int.
    gauge.setProperty("level", 7.9);
    CHECK_EQ(gauge.level(), 7);
    CHECK_EQ(lastValue, 7);

    // Incompatible type -> std::invalid_argument.
    CHECK_THROWS_AS(gauge.setProperty("level", std::string("nope")),
                    std::invalid_argument);

    // The native setter also notifies.
    gauge.setLevel(100);
    CHECK_EQ(calls, 3);
    CHECK_EQ(lastValue, 100);

    // Duplicate registration is rejected.
    class Bad : public Object {
    public:
        Bad() {
            registerProperty("v", &Bad::v, &Bad::setV);
            registerProperty("v", &Bad::v, &Bad::setV);
        }
        int v() const { return 0; }
        void setV(int) {}
    };
    CHECK_THROWS_AS(Bad{}, std::invalid_argument);
}

TEST_CASE(object_destroyed_signal) {
    Object watched;
    bool destroyed = false;
    watched.connect(&watched, Object::signalDestroyed,
                    [&] { destroyed = true; }, ConnectionMode::Direct);
    // A scoped child proves the signal fires at destruction start.
    {
        Object child(&watched);
        child.connect(&child, Object::signalDestroyed,
                      [&] { destroyed = true; }, ConnectionMode::Direct);
    }
    CHECK(destroyed);
}

TEST_CASE(object_property_meta_list) {
    class Meta : public Object {
    public:
        Meta() { registerProperty("value", &Meta::value, &Meta::setValue); }
        double value() const { return 0; }
        void setValue(double) {}
    };
    Meta meta;
    meta.setProperty("zz_dynamic", 1);
    const auto list = meta.propertyMetaList();
    CHECK_EQ(list.size(), 2u);
    CHECK_EQ(list[0].name, "value"); // registered first, registration order
    CHECK(*list[0].type == typeid(double));
    // dynamic entries come after the registered ones, sorted by name
    CHECK_EQ(list[1].name, "zz_dynamic");
    CHECK(*list[1].type == typeid(int));
}
