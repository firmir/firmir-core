// Selection rules behind FIRMIR_GUI_DISPLAY / FIRMIR_GUI_INPUT /
// FIRMIR_GUI_BACKENDS (firmir/gui/detail/backendselection.hpp). The code is
// pure: no plugins are loaded, the platform singleton is not touched.

#include "test_framework.hpp"

#include <firmir/gui/detail/backendselection.hpp>

#include <string>
#include <vector>

using namespace Firmir::Gui::detail;

namespace {

std::string joined(const std::vector<std::string>& names) {
    std::string result;
    for (const std::string& name : names) {
        if (!result.empty())
            result += ',';
        result += name;
    }
    return result;
}

} // namespace

TEST_CASE(gui_backend_list_parsing) {
    CHECK_EQ(joined(parseBackendList("drm,eglfs")), std::string("drm,eglfs"));
    // Whitespace and case do not matter; empty items are dropped.
    CHECK_EQ(joined(parseBackendList(" Wayland , ,DRM ,")), std::string("wayland,drm"));
    // The first occurrence wins; duplicates are removed.
    CHECK_EQ(joined(parseBackendList("drm,drm,eglfs")), std::string("drm,eglfs"));
    CHECK_EQ(joined(parseBackendList("headless, headless")), std::string("headless"));
    // Unset/blank and unknown names: empty list resp. name preserved for the
    // caller to report.
    CHECK(parseBackendList("").empty());
    CHECK(parseBackendList(" , ,").empty());
    CHECK_EQ(joined(parseBackendList("vulkan")), std::string("vulkan"));
}

TEST_CASE(gui_backend_default_list) {
    // Defaults when FIRMIR_GUI_BACKENDS is unset.
    CHECK_EQ(joined(defaultBackendList()),
             std::string("wayland,eglfs,drm,evdev,headless"));
    CHECK(isHardwareDisplayBackend("drm"));
    CHECK(isHardwareDisplayBackend("eglfs"));
    CHECK(!isHardwareDisplayBackend("wayland"));
    CHECK(!isHardwareDisplayBackend("headless"));
    CHECK(!isHardwareDisplayBackend("evdev"));
}

TEST_CASE(gui_backend_name_validation) {
    CHECK(isDisplayBackendName("headless"));
    CHECK(isDisplayBackendName("wayland"));
    CHECK(isDisplayBackendName("drm"));
    CHECK(isDisplayBackendName("eglfs"));
    CHECK(!isDisplayBackendName("evdev"));
    CHECK(!isDisplayBackendName(""));

    CHECK(isInputBackendName("headless"));
    CHECK(isInputBackendName("wayland"));
    CHECK(isInputBackendName("evdev"));
    CHECK(!isInputBackendName("drm"));
    CHECK(!isInputBackendName("eglfs"));
}

TEST_CASE(gui_display_backend_candidates) {
    const std::vector<std::string> none;
    std::vector<std::string> skipped;
    // FIRMIR_GUI_DISPLAY pins a backend and disables the fallback chain; the
    // pin is not filtered by device availability.
    CHECK_EQ(joined(displayBackendCandidates("drm", {"eglfs"}, true, true)),
             std::string("drm"));
    CHECK_EQ(joined(displayBackendCandidates("drm", none, false, false)),
             std::string("drm"));
    // FIRMIR_GUI_BACKENDS order is preserved; input-only names are ignored.
    CHECK_EQ(joined(displayBackendCandidates("", {"eglfs", "drm"}, true, true)),
             std::string("eglfs,drm"));
    CHECK_EQ(joined(displayBackendCandidates("", {"evdev", "drm"}, true, true)),
             std::string("drm"));
    // Names whose device is missing are skipped and reported to the caller.
    skipped.clear();
    CHECK_EQ(joined(displayBackendCandidates("", {"wayland", "drm"}, false, true, &skipped)),
             std::string("drm"));
    CHECK_EQ(joined(skipped), std::string("wayland"));
    // Nothing in the list can work: empty result (the caller errors out)
    // instead of silently running another backend.
    skipped.clear();
    CHECK(displayBackendCandidates("", {"drm", "eglfs"}, false, false, &skipped).empty());
    CHECK_EQ(joined(skipped), std::string("drm,eglfs"));
    // Only input backends requested: the default list applies.
    CHECK_EQ(joined(displayBackendCandidates("", {"evdev"}, false, false)),
             std::string("headless"));
    // No list: defaultBackendList() gated by device availability
    // (wayland, eglfs, drm, headless).
    CHECK_EQ(joined(displayBackendCandidates("", none, true, true)),
             std::string("wayland,eglfs,drm,headless"));
    CHECK_EQ(joined(displayBackendCandidates("", none, false, true)),
             std::string("eglfs,drm,headless"));
    CHECK_EQ(joined(displayBackendCandidates("", none, true, false)),
             std::string("wayland,headless"));
    CHECK_EQ(joined(displayBackendCandidates("", none, false, false)),
             std::string("headless"));
    // Default list with the wayland hardware present: same order as written in
    // FIRMIR_GUI_BACKENDS=wayland,eglfs,drm,evdev,headless.
    skipped.clear();
    CHECK_EQ(joined(displayBackendCandidates("", defaultBackendList(), true, true)),
             std::string("wayland,eglfs,drm,headless"));
    CHECK(skipped.empty());
    // displayBackendAvailable() mirrors the automatic chain's gating.
    CHECK(displayBackendAvailable("headless", false, false));
    CHECK(displayBackendAvailable("wayland", true, false));
    CHECK(!displayBackendAvailable("wayland", false, true));
    CHECK(displayBackendAvailable("drm", false, true));
    CHECK(displayBackendAvailable("eglfs", false, true));
    CHECK(!displayBackendAvailable("eglfs", true, false));
}

TEST_CASE(gui_input_backend_candidates) {
    const std::vector<std::string> none;
    // FIRMIR_GUI_INPUT pins the input backend.
    CHECK_EQ(joined(inputBackendCandidates("evdev", {"headless"}, "drm", false)),
             std::string("evdev"));
    // FIRMIR_GUI_BACKENDS: input-capable names in list order, display-only ones
    // are ignored.
    CHECK_EQ(joined(inputBackendCandidates("", {"drm", "evdev", "headless"}, "drm", false)),
             std::string("evdev,headless"));
    CHECK_EQ(joined(inputBackendCandidates("", {"evdev", "wayland"}, "wayland", true)),
             std::string("evdev,wayland,headless"));
    // evdev works alongside the hardware backends: even a list without it gets
    // evdev for drm/eglfs.
    CHECK_EQ(joined(inputBackendCandidates("", {"eglfs", "headless"}, "eglfs", false)),
             std::string("evdev,headless"));
    CHECK_EQ(joined(inputBackendCandidates("", {"drm"}, "drm", false)),
             std::string("evdev,headless"));
    // ... but never "instead of" the hardware backend itself (display role).
    CHECK_EQ(joined(displayBackendCandidates("", {"eglfs", "evdev"}, false, true)),
             std::string("eglfs"));
    // Wayland input belongs to the Wayland display connection: it is dropped
    // when another display backend won.
    CHECK_EQ(joined(inputBackendCandidates("", {"wayland", "evdev"}, "drm", false)),
             std::string("evdev,headless"));
    // The display plugin's own input factory comes first for wayland/headless.
    CHECK_EQ(joined(inputBackendCandidates("", none, "wayland", true)),
             std::string("wayland,evdev,headless"));
    CHECK_EQ(joined(inputBackendCandidates("", none, "headless", true)),
             std::string("headless,evdev"));
    // Default list for a hardware display.
    CHECK_EQ(joined(inputBackendCandidates("", defaultBackendList(), "drm", false)),
             std::string("evdev,headless"));
    CHECK_EQ(joined(inputBackendCandidates("", defaultBackendList(), "eglfs", false)),
             std::string("evdev,headless"));
}
