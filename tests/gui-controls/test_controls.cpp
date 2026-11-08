// Unit tests for the gui-controls widgets (buttons, check boxes, radio
// groups, switch, indicators, containers) — behavior only, no rendering.

#include "test_framework.hpp"

#include <firmir/gui-controls/controls.hpp>
#include <firmir/gui/item.hpp>

#include <any>
#include <cmath>

using namespace Firmir;
using namespace Firmir::Gui;

TEST_CASE(controls_button_click_and_toggle) {
    Controls::Button button;
    button.setText("hello");
    CHECK(button.text() == String("hello"));
    CHECK(!button.checkable());
    CHECK(!button.isChecked());

    int clicks = 0;
    int toggles = 0;
    button.connect(&button, Controls::AbstractButton::signalClicked,
                   [&clicks] { ++clicks; });
    button.connect<bool>(&button, Controls::AbstractButton::signalToggled,
                         [&toggles](bool) { ++toggles; });

    button.click();
    CHECK_EQ(clicks, 1);
    CHECK_EQ(toggles, 0); // not checkable: no toggling
    CHECK(!button.isChecked());

    button.setCheckable(true);
    button.click();
    CHECK_EQ(clicks, 2);
    CHECK_EQ(toggles, 1);
    CHECK(button.isChecked());
    button.click();
    CHECK_EQ(toggles, 2);
    CHECK(!button.isChecked());
}

TEST_CASE(controls_checkbox_states) {
    Controls::CheckBox box;
    CHECK(box.checkState() == Controls::CheckBox::CheckState::Unchecked);
    CHECK(!box.tristate());

    box.click(); // unchecked -> checked (not tristate)
    CHECK(box.checkState() == Controls::CheckBox::CheckState::Checked);
    CHECK(box.isChecked());
    box.click();
    CHECK(box.checkState() == Controls::CheckBox::CheckState::Unchecked);

    // Tristate cycle: unchecked -> partially -> checked -> unchecked.
    box.setTristate(true);
    box.click();
    CHECK(box.checkState() == Controls::CheckBox::CheckState::PartiallyChecked);
    CHECK(!box.isChecked());
    box.click();
    CHECK(box.checkState() == Controls::CheckBox::CheckState::Checked);
    box.click();
    CHECK(box.checkState() == Controls::CheckBox::CheckState::Unchecked);

    // setCheckState keeps the plain checked flag in sync.
    box.setCheckState(Controls::CheckBox::CheckState::Checked);
    CHECK(box.isChecked());
    box.setCheckState(Controls::CheckBox::CheckState::PartiallyChecked);
    CHECK(!box.isChecked());
}

TEST_CASE(controls_radio_auto_exclusive) {
    Item group;
    Controls::RadioButton first(&group);
    Controls::RadioButton second(&group);
    Controls::RadioButton detached; // no shared parent: not in the group

    first.click();
    CHECK(first.isChecked());
    CHECK(!second.isChecked());

    second.click();
    CHECK(second.isChecked());
    CHECK(!first.isChecked()); // auto-exclusive within the parent

    detached.click();
    CHECK(detached.isChecked());
    CHECK(second.isChecked()); // different group: untouched

    // Non-exclusive mode toggles independently.
    second.setAutoExclusive(false);
    second.click();
    CHECK(!second.isChecked());
}

TEST_CASE(controls_switch_toggles) {
    Controls::Switch sw;
    CHECK(!sw.isChecked());
    sw.click();
    CHECK(sw.isChecked());
    sw.click();
    CHECK(!sw.isChecked());
}

TEST_CASE(controls_progressbar) {
    Controls::ProgressBar bar;
    CHECK_EQ(bar.value(), 0.0f);
    CHECK_EQ(bar.from(), 0.0f);
    CHECK_EQ(bar.to(), 1.0f);

    bar.setValue(0.5f);
    CHECK(std::fabs(bar.visualPosition() - 0.5f) < 0.001f);

    // Custom range: 25 of 0..100 -> 0.25.
    bar.setTo(100.0f);
    bar.setValue(25.0f);
    CHECK(std::fabs(bar.visualPosition() - 0.25f) < 0.001f);

    // Values are clamped to the range.
    bar.setValue(250.0f);
    CHECK(std::fabs(bar.visualPosition() - 1.0f) < 0.001f);
    bar.setValue(-5.0f);
    CHECK(std::fabs(bar.visualPosition()) < 0.001f);

    bar.setIndeterminate(true);
    CHECK(bar.indeterminate());
}

TEST_CASE(controls_tabbar) {
    Controls::TabBar tabs;
    tabs.addTab("one");
    tabs.addTab("two");
    tabs.addTab("three");
    CHECK_EQ(tabs.count(), 3);
    CHECK_EQ(tabs.currentIndex(), 0); // the first tab becomes current
    CHECK(tabs.textAt(1) == String("two"));

    int changes = 0;
    tabs.connect<int>(&tabs, Controls::TabBar::signalCurrentIndexChanged,
                      [&changes](int) { ++changes; });

    // Clicking a tab's button selects it.
    tabs.tabAt(2)->click();
    CHECK_EQ(tabs.currentIndex(), 2);
    CHECK(changes > 0);
    CHECK(tabs.tabAt(2)->isChecked());
    CHECK(!tabs.tabAt(0)->isChecked());

    // The registered property path works too (used from Firml).
    tabs.setProperty("currentIndex", 1.0);
    CHECK_EQ(tabs.currentIndex(), 1);
    CHECK(std::any_cast<int>(tabs.getProperty("count")) == 3);
}

TEST_CASE(controls_stackview) {
    Controls::StackView stack;
    Item first;
    Item second;

    CHECK_EQ(stack.depth(), 0);
    CHECK(stack.currentItem() == nullptr);

    stack.push(&first);
    CHECK_EQ(stack.depth(), 1);
    CHECK(stack.currentItem() == &first);
    CHECK(first.isVisible());

    stack.push(&second);
    CHECK_EQ(stack.depth(), 2);
    CHECK(stack.currentItem() == &second);
    CHECK(!first.isVisible()); // only the top page is visible

    Item* popped = stack.pop();
    CHECK(popped == &second);
    CHECK_EQ(stack.depth(), 1);
    CHECK(first.isVisible());

    stack.replace(&second);
    CHECK_EQ(stack.depth(), 1);
    CHECK(stack.currentItem() == &second);

    stack.clear();
    CHECK_EQ(stack.depth(), 0);
    CHECK(stack.currentItem() == nullptr);
}

TEST_CASE(controls_swipeview_pages) {
    Controls::SwipeView view;
    Item a;
    Item b;
    Item c;
    view.addItem(&a);
    view.addItem(&b);
    view.addItem(&c);
    CHECK_EQ(view.count(), 3);
    CHECK_EQ(view.currentIndex(), 0);
    CHECK(view.currentItem() == &a);
    CHECK(a.isVisible());
    CHECK(!b.isVisible());

    view.setCurrentIndex(2);
    CHECK_EQ(view.currentIndex(), 2);
    CHECK(view.currentItem() == &c);
    CHECK(c.isVisible());
    CHECK(!a.isVisible());

    // Out-of-range indices are ignored.
    view.setCurrentIndex(5);
    CHECK_EQ(view.currentIndex(), 2);
    view.setCurrentIndex(-1);
    CHECK_EQ(view.currentIndex(), 2);

    CHECK(view.interactive());
    view.setInteractive(false);
    CHECK(!view.interactive());
}

TEST_CASE(controls_splitview_sizes) {
    Controls::SplitView split;
    split.setWidth(400);
    split.setHeight(300);
    Item left;
    Item right;
    split.addItem(&left);
    split.addItem(&right);
    CHECK_EQ(split.count(), 2);
    CHECK(split.itemAt(0) == &left);

    split.setSizes({100.0f, 300.0f});
    const auto sizes = split.sizes();
    CHECK_EQ(sizes.size(), size_t(2));
    CHECK(std::fabs(sizes[0] - 100.0f) < 0.01f);
    CHECK(std::fabs(sizes[1] - 300.0f) < 0.01f);

    // The layout actually places the children next to each other.
    CHECK(left.width() > 0.0f);
    CHECK(right.x() > left.x());

    // Vertical orientation stacks along Y.
    split.setOrientation(Controls::SplitView::Orientation::Vertical);
    CHECK(split.orientation() == Controls::SplitView::Orientation::Vertical);
    CHECK(right.y() > left.y());
}

TEST_CASE(controls_busyindicator_running) {
    Controls::BusyIndicator indicator;
    CHECK(indicator.running()); // spins by default
    indicator.setRunning(false);
    CHECK(!indicator.running());
    indicator.setRunning(true);
    CHECK(indicator.running());
}
