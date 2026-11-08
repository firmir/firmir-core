#include "test_framework.hpp"

#include <firmir/gui/item.hpp>
#include <firmir/gui/listmodel.hpp>
#include <firmir/gui/positioners.hpp>
#include <firmir/gui/repeater.hpp>

using namespace Firmir;
using namespace Firmir::Gui;

namespace {

Item* box(Object* parent, float w, float h) {
    auto* item = new Item(parent);
    item->setWidth(w);
    item->setHeight(h);
    return item;
}

} // namespace

TEST_CASE(positioners_column) {
    Column column;
    column.setSpacing(4);
    Item* a = box(&column, 10, 20);
    Item* b = box(&column, 30, 10);
    CHECK_EQ(column.implicitWidth(), 30);
    CHECK_EQ(column.implicitHeight(), 34); // 20 + 4 + 10
    CHECK_EQ(a->x(), 0);
    CHECK_EQ(a->y(), 0);
    CHECK_EQ(b->y(), 24);

    // Height change of a child relayouts the rest.
    a->setHeight(30);
    CHECK_EQ(b->y(), 34);
    CHECK_EQ(column.implicitHeight(), 44);

    // A child added later joins the layout.
    Item* c = box(&column, 5, 5);
    CHECK_EQ(c->y(), 48); // 34 + 10 + 4 spacing
    CHECK_EQ(column.implicitHeight(), 53);

    // Hidden children drop out of the layout.
    b->setVisible(false);
    CHECK_EQ(c->y(), 34);
    CHECK_EQ(column.implicitHeight(), 39);
}

TEST_CASE(positioners_row) {
    Row row;
    row.setSpacing(2);
    Item* a = box(&row, 10, 20);
    Item* b = box(&row, 30, 10);
    CHECK_EQ(row.implicitWidth(), 42);
    CHECK_EQ(row.implicitHeight(), 20);
    CHECK_EQ(a->x(), 0);
    CHECK_EQ(b->x(), 12);
    a->setWidth(20);
    CHECK_EQ(b->x(), 22);
}

TEST_CASE(positioners_grid) {
    Grid grid;
    grid.setColumns(3);
    grid.setSpacing(1);
    // Four equal cells; the small child is centered in its cell.
    Item* a = box(&grid, 10, 10);
    box(&grid, 10, 10);
    box(&grid, 10, 10);
    Item* d = box(&grid, 6, 8);
    CHECK_EQ(a->x(), 0);
    CHECK_EQ(a->y(), 0);
    CHECK_EQ(d->x(), 2);  // cell 0 of row 1 + (10 - 6) / 2
    CHECK_EQ(d->y(), 12); // row 1 + (10 - 8) / 2
    CHECK_EQ(grid.implicitWidth(), 32);  // 3 * 10 + 2 * 1
    CHECK_EQ(grid.implicitHeight(), 21); // 2 * 10 + 1

    // rows-derivation: with 4 children and rows=2 the column count is 2.
    grid.setColumns(0);
    grid.setRows(2);
    box(&grid, 10, 10); // fifth child
    // 5 children / 2 rows -> 3 columns.
    CHECK_EQ(grid.implicitWidth(), 32);
}

TEST_CASE(positioners_repeater_children) {
    // Delegates inside a Repeater are positioned as if direct children.
    Column column;
    auto* repeater = new Repeater(&column);
    Item* a = box(repeater, 10, 10);
    Item* b = box(repeater, 10, 10);
    CHECK_EQ(a->y(), 0);
    CHECK_EQ(b->y(), 10);
    // A delegate added later relayouts the column.
    Item* c = box(repeater, 10, 10);
    CHECK_EQ(c->y(), 20);
    CHECK_EQ(column.implicitHeight(), 30);
}

TEST_CASE(positioners_listmodel) {
    ListModel model;
    model.append({{"label", std::string("A")}, {"value", 7}});
    model.append({{"label", std::string("B")}});
    CHECK_EQ(model.count(), 2);
    CHECK_EQ(std::any_cast<std::string>(model.data(0, "label")), "A");
    CHECK_EQ(std::any_cast<int>(model.data(0, "value")), 7);
    // A role missing in a sparse row falls back to the type default; an
    // unknown role or a row out of range yields an empty any.
    CHECK_EQ(std::any_cast<double>(model.data(1, "value")), 0.0);
    CHECK(!model.data(0, "unknown").has_value());
    CHECK(!model.data(2, "label").has_value());
    CHECK_EQ(std::any_cast<std::string>(model.itemAt(1).at("label")), "B");
}
