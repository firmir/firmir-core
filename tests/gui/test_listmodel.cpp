// Unit tests for Gui::ListModel: row storage, role defaults for sparse
// rows, the count property and its change signal (drives firml Repeaters).

#include "test_framework.hpp"

#include <firmir/gui/listmodel.hpp>
#include <firmir/gui/painter.hpp> // Color

#include <any>
#include <string>

using namespace Firmir;
using namespace Firmir::Gui;

TEST_CASE(listmodel_append_and_count_signal) {
    ListModel model;
    CHECK_EQ(model.count(), 0);

    int countChanges = 0;
    model.connect<std::string, std::any>(
        &model, Object::signalPropertyChanged,
        [&countChanges](const std::string& name, const std::any&) {
            if (name == "count")
                ++countChanges;
        },
        ConnectionMode::Direct);

    model.append({{"label", String("one")}, {"size", 10.0}});
    model.append({{"label", String("two")}, {"size", 20.0}});
    CHECK_EQ(model.count(), 2);
    CHECK_EQ(countChanges, 2);

    // data() returns the stored values.
    CHECK(std::any_cast<String>(model.data(0, "label")) == String("one"));
    CHECK_EQ(std::any_cast<double>(model.data(1, "size")), 20.0);
}

TEST_CASE(listmodel_sparse_rows_get_role_defaults) {
    ListModel model;
    model.append({{"label", String("full")},
                  {"size", 42.0},
                  {"badge", Color::rgb(1, 2, 3)}});
    model.append({{"label", String("sparse")}}); // no size, no badge

    // Sparse roles default to the learned type's zero value.
    CHECK_EQ(std::any_cast<double>(model.data(1, "size")), 0.0);
    CHECK(std::any_cast<Color>(model.data(1, "badge")) == Color());

    // A role nobody ever declared yields an empty any.
    CHECK(!model.data(0, "nonexistent").has_value());

    // clear() removes the rows but keeps the role schema: a sparse row
    // appended afterwards still gets the learned default.
    model.clear();
    CHECK_EQ(model.count(), 0);
    model.append({{"label", String("x")}});
    CHECK_EQ(std::any_cast<double>(model.data(0, "size")), 0.0);
}

TEST_CASE(listmodel_clear_emits_count) {
    ListModel model;
    model.append({{"n", 1.0}});
    model.append({{"n", 2.0}});

    int countChanges = 0;
    model.connect<std::string, std::any>(
        &model, Object::signalPropertyChanged,
        [&countChanges](const std::string& name, const std::any&) {
            if (name == "count")
                ++countChanges;
        },
        ConnectionMode::Direct);
    model.clear();
    CHECK_EQ(model.count(), 0);
    CHECK_EQ(countChanges, 1); // one signal for the whole clear

    // Clearing an empty model does not signal anything.
    model.clear();
    CHECK_EQ(countChanges, 1);
}

TEST_CASE(listmodel_insert_remove_signals) {
    ListModel model;
    int inserted = 0;
    int removed = 0;
    model.connect<int, int>(&model, "itemsInserted",
                            [&inserted](int, int) { ++inserted; },
                            ConnectionMode::Direct);
    model.connect<int, int>(&model, "itemsRemoved",
                            [&removed](int, int) { ++removed; },
                            ConnectionMode::Direct);
    model.append({{"n", 1.0}});
    CHECK_EQ(inserted, 1);
    CHECK_EQ(removed, 0);
    model.clear();
    CHECK_EQ(removed, 1);
}
