// E2E test driver for firmlc-generated Repeater code: builds the repeater.fml
// item tree, checks the delegate instantiation, the Grid layout over the
// delegates and the model.<role>/index handling, exits non-zero on mismatch.

#include "repeater.hpp"

#include <firmir/gui/positioners.hpp>
#include <firmir/gui/repeater.hpp>
#include <firmir/string.hpp>

#include <any>
#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void checkEq(const char* what, float actual, float expected) {
    if (std::fabs(actual - expected) > 0.001f) {
        std::printf("FAIL %s: %.2f != %.2f\n", what, actual, expected);
        ++failures;
    }
}

} // namespace

int main() {
    RepeaterObjects o = createRepeater(nullptr);

    // The model was populated from the ListElement declarations.
    if (o.keys->count() != 4) {
        std::printf("FAIL keys.count: %d != 4\n", o.keys->count());
        ++failures;
    }

    // One delegate per row, created as children of the Repeater.
    const auto delegates = o.rep->childItems();
    if (delegates.size() != 4) {
        std::printf("FAIL delegates: %zu != 4\n", delegates.size());
        return 1;
    }

    // model.<role> bindings, including the sparse 'badge' role default.
    for (const char* expected : {"a", "b", "c", "d"}) {
        const int i = expected[0] - 'a';
        if (std::any_cast<Firmir::String>(
                delegates[size_t(i)]->getProperty("text")) != Firmir::String(expected)) {
            std::printf("FAIL delegate %d text\n", i);
            ++failures;
        }
    }
    if (std::any_cast<Firmir::Gui::Color>(delegates[0]->getProperty("color")) !=
        Firmir::Gui::Color()) { // no 'badge' in row 0 -> default color
        std::printf("FAIL delegate 0 color (sparse role default)\n");
        ++failures;
    }
    if (std::any_cast<Firmir::Gui::Color>(delegates[2]->getProperty("color")) !=
        Firmir::Gui::Color::rgb(0xFF, 0x00, 0x00)) {
        std::printf("FAIL delegate 2 color\n");
        ++failures;
    }

    // The Grid lays out the repeater's delegates: 30px cells, 10px spacing,
    // the 20px delegates centered in their cells.
    checkEq("d0.x", delegates[0]->x(), 5.0f);
    checkEq("d0.y", delegates[0]->y(), 5.0f);
    checkEq("d1.x", delegates[1]->x(), 45.0f); // 30 + 10 + 5
    checkEq("d1.y", delegates[1]->y(), 5.0f);
    checkEq("d2.x", delegates[2]->x(), 0.0f);
    checkEq("d2.y", delegates[2]->y(), 40.0f); // 30 + 10
    checkEq("grid.implicitWidth", o.grid->implicitWidth(), 70.0f);
    checkEq("grid.implicitHeight", o.grid->implicitHeight(), 70.0f);

    // The delegate handler sees the row index.
    delegates[1]->emitSignal("clicked");
    checkEq("root.selected after click",
            float(std::any_cast<double>(o.root->getProperty("selected"))), 2.0f);

    if (failures)
        std::printf("%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
