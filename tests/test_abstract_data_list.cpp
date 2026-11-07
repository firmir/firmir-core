#include "test_framework.hpp"

#include <firmir/gui/abstractdatalist.hpp>

#include <any>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Firmir;
using namespace Firmir::Gui;

namespace {

/// Test double exposing the protected begin/end API.
class TagModel : public AbstractDataList<std::string> {
public:
    using AbstractDataList::AbstractDataList;
    using AbstractDataList::beginInsertItems;
    using AbstractDataList::beginMoveItems;
    using AbstractDataList::beginRemoveItems;
    using AbstractDataList::beginResetModel;
    using AbstractDataList::clearItems;
    using AbstractDataList::endInsertItems;
    using AbstractDataList::endMoveItems;
    using AbstractDataList::endRemoveItems;
    using AbstractDataList::endResetModel;

    void append(std::initializer_list<const char*> tags) {
        beginInsertItems(count(), count() + static_cast<int>(tags.size()) - 1);
        endInsertItems();
        int index = count() - static_cast<int>(tags.size());
        for (const char* tag : tags)
            itemAt(index++) = tag;
    }

    std::vector<std::string> flat() const {
        std::vector<std::string> out;
        for (int i = 0; i < count(); ++i)
            out.push_back(itemAt(i));
        return out;
    }
};

std::vector<std::string> tags(std::initializer_list<const char*> in) {
    std::vector<std::string> out;
    for (const char* tag : in)
        out.emplace_back(tag);
    return out;
}

} // namespace

TEST_CASE(abstract_data_list_basics) {
    TagModel model;
    CHECK_EQ(model.count(), 0);
    // Registered read-only property.
    CHECK_EQ(std::any_cast<int>(model.getProperty("count")), 0);
    CHECK_THROWS_AS(model.setProperty("count", std::any(3)),
                    std::invalid_argument);

    // itemAt bounds checking.
    CHECK_THROWS_AS(model.itemAt(0), std::out_of_range);
    CHECK_THROWS_AS(model.itemAt(-1), std::out_of_range);

    int countNotifications = 0;
    model.connect<std::string, std::any>(
        &model, Object::signalPropertyChanged,
        [&countNotifications](const std::string& name, const std::any&) {
            if (name == "count")
                ++countNotifications;
        },
        ConnectionMode::Direct);

    model.append({"a", "b"});
    model.append({"c"});
    CHECK_EQ(model.count(), 3);
    CHECK(model.flat() == tags({"a", "b", "c"}));
    CHECK_EQ(std::any_cast<int>(model.getProperty("count")), 3);
    CHECK(countNotifications >= 2);
    CHECK_EQ(model.itemAt(1), "b");
    model.itemAt(1) = "B";
    CHECK_EQ(model.itemAt(1), "B");
}

TEST_CASE(abstract_data_list_insert_remove) {
    TagModel model;
    model.append({"a", "d"});

    std::vector<std::pair<int, int>> aboutToInsert;
    std::vector<std::pair<int, int>> inserted;
    model.connect<int, int>(&model, TagModel::signalItemsAboutToBeInserted,
                            [&aboutToInsert](int first, int last) {
                                aboutToInsert.emplace_back(first, last);
                            },
                            ConnectionMode::Direct);
    model.connect<int, int>(&model, TagModel::signalItemsInserted,
                            [&inserted](int first, int last) {
                                inserted.emplace_back(first, last);
                            },
                            ConnectionMode::Direct);

    model.beginInsertItems(1, 2); // two items before "d"
    model.endInsertItems();
    CHECK_EQ(model.count(), 4);
    CHECK((aboutToInsert == std::vector<std::pair<int, int>>{{1, 2}}));
    CHECK((inserted == std::vector<std::pair<int, int>>{{1, 2}}));
    model.itemAt(1) = "b";
    model.itemAt(2) = "c";
    CHECK(model.flat() == tags({"a", "b", "c", "d"}));

    model.beginRemoveItems(2, 2);
    model.endRemoveItems();
    CHECK_EQ(model.count(), 3);
    CHECK(model.flat() == tags({"a", "b", "d"}));

    // Appending at the very end is allowed (first == count).
    model.beginInsertItems(3, 3);
    model.endInsertItems();
    CHECK_EQ(model.count(), 4);

    // Invalid ranges.
    CHECK_THROWS_AS(model.beginInsertItems(-1, 0), std::out_of_range);
    CHECK_THROWS_AS(model.beginInsertItems(2, 1), std::out_of_range);
    CHECK_THROWS_AS(model.beginInsertItems(5, 5), std::out_of_range); // > count
    CHECK_THROWS_AS(model.beginRemoveItems(3, 4), std::out_of_range);
    // end without begin / mismatched end / nested begin.
    CHECK_THROWS_AS(model.endRemoveItems(), std::logic_error);
    model.beginRemoveItems(3, 3);
    CHECK_THROWS_AS(model.endInsertItems(), std::logic_error);
    CHECK_THROWS_AS(model.beginInsertItems(0, 0), std::logic_error); // nested
    model.endRemoveItems();
    CHECK_EQ(model.count(), 3);
}

TEST_CASE(abstract_data_list_move) {
    TagModel model;
    model.append({"a", "b", "c", "d"});

    int moveArgs[3] = {-1, -1, -1};
    model.connect<int, int, int>(&model, TagModel::signalItemsMoved,
                                 [&moveArgs](int first, int last, int dest) {
                                     moveArgs[0] = first;
                                     moveArgs[1] = last;
                                     moveArgs[2] = dest;
                                 },
                                 ConnectionMode::Direct);

    // Move "a" below "b": [B A C D].
    model.beginMoveItems(0, 0, 1);
    model.endMoveItems();
    CHECK(model.flat() == tags({"b", "a", "c", "d"}));
    CHECK_EQ(moveArgs[0], 0);
    CHECK_EQ(moveArgs[1], 0);
    CHECK_EQ(moveArgs[2], 1);

    // Move the two top items to the end: [C D B A].
    model.beginMoveItems(0, 1, 2);
    model.endMoveItems();
    CHECK(model.flat() == tags({"c", "d", "b", "a"}));

    // Destination validation: the block must fit.
    CHECK_THROWS_AS(model.beginMoveItems(0, 1, 3), std::out_of_range);
    CHECK_THROWS_AS(model.beginMoveItems(0, 0, 4), std::out_of_range);
    CHECK_THROWS_AS(model.beginMoveItems(1, 3, 2), std::out_of_range);
}

TEST_CASE(abstract_data_list_reset) {
    TagModel model;
    model.append({"a", "b"});

    std::vector<std::string> signals;
    model.connect(&model, TagModel::signalModelAboutToBeReset,
                  [&signals] { signals.emplace_back("aboutToReset"); },
                  ConnectionMode::Direct);
    model.connect(&model, TagModel::signalModelReset,
                  [&signals] { signals.emplace_back("reset"); },
                  ConnectionMode::Direct);

    model.beginResetModel();
    model.clearItems();
    model.endResetModel();
    CHECK_EQ(model.count(), 0);
    CHECK((signals == std::vector<std::string>{"aboutToReset", "reset"}));

    // Repopulate after the reset.
    model.append({"z"});
    CHECK_EQ(model.count(), 1);
    CHECK_EQ(model.itemAt(0), "z");
}

