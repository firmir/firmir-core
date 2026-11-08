// Unit test for the wl_keyboard keycode translation used by the Wayland GUI
// plugin (plugins/gui/wayland/keytranslate.hpp). The wire value of
// wl_keyboard.key is an evdev keycode; xkbcommon indexes by XKB keycodes
// (evdev + 8). Mixing the two numberings shifts every key by one position
// row: Backspace was typed as "5" and the ЙЦУКЕН 'о' key acted as Enter.

#include "test_framework.hpp"

#include "../../plugins/gui/wayland/keytranslate.hpp"

#include <cstring>

using namespace Firmir;

namespace {

struct Xkb {
    xkb_context* ctx = nullptr;
    xkb_keymap* keymap = nullptr;
    xkb_state* state = nullptr;

    explicit Xkb(const char* layout) {
        ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!ctx)
            return;
        const xkb_rule_names rules{"evdev", "pc105", layout, "", nullptr};
        keymap = xkb_keymap_new_from_names(ctx, &rules,
                                           XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (keymap)
            state = xkb_state_new(keymap);
    }
    ~Xkb() {
        if (state)
            xkb_state_unref(state);
        if (keymap)
            xkb_keymap_unref(keymap);
        if (ctx)
            xkb_context_unref(ctx);
    }
    Xkb(const Xkb&) = delete;
    Xkb& operator=(const Xkb&) = delete;
};

constexpr std::uint32_t KeyBackspace = 14;
constexpr std::uint32_t KeyEnter = 28;
constexpr std::uint32_t KeyTab = 15;
constexpr std::uint32_t Key5 = 6;
constexpr std::uint32_t KeyJ = 36; // ЙЦУКЕН 'о' position
constexpr std::uint32_t KeyA = 30;
constexpr std::uint32_t KeyLeft = 105;
constexpr std::uint32_t KeyDelete = 111;

} // namespace

TEST_CASE(wayland_keymap_editing_keys) {
    Xkb xkb("us");
    CHECK(xkb.state != nullptr);
    if (!xkb.state)
        return;

    // Backspace: editing key, never printable text (the "5" bug).
    auto bs = WaylandInput::translateKey(xkb.state, KeyBackspace, true);
    CHECK_EQ(bs.key, 14);
    CHECK(bs.text.empty());

    // Enter and Tab: editing keys, no text.
    CHECK_EQ(WaylandInput::translateKey(xkb.state, KeyEnter, true).key, 28);
    CHECK(WaylandInput::translateKey(xkb.state, KeyEnter, true).text.empty());
    CHECK_EQ(WaylandInput::translateKey(xkb.state, KeyTab, true).key, 15);
    CHECK(WaylandInput::translateKey(xkb.state, KeyTab, true).text.empty());

    // Arrows / Delete: normalized by keysym.
    CHECK_EQ(WaylandInput::translateKey(xkb.state, KeyLeft, true).key, 105);
    CHECK_EQ(WaylandInput::translateKey(xkb.state, KeyDelete, true).key, 111);
}

TEST_CASE(wayland_keymap_printable_text) {
    Xkb xkb("us");
    CHECK(xkb.state != nullptr);
    if (!xkb.state)
        return;

    // Digits and letters produce text on press, keeping their evdev code.
    const auto five = WaylandInput::translateKey(xkb.state, Key5, true);
    CHECK_EQ(five.key, 6);
    CHECK_EQ(five.text, std::string("5"));

    const auto a = WaylandInput::translateKey(xkb.state, KeyA, true);
    CHECK_EQ(a.key, 30);
    CHECK_EQ(a.text, std::string("a"));

    // Releases carry no text.
    CHECK(WaylandInput::translateKey(xkb.state, KeyA, false).text.empty());
    CHECK_EQ(WaylandInput::translateKey(xkb.state, KeyA, false).key, 30);

    // No xkb state (keymap not received yet): the evdev code still arrives.
    const auto raw = WaylandInput::translateKey(nullptr, KeyA, true);
    CHECK_EQ(raw.key, 30);
    CHECK(raw.text.empty());
}

TEST_CASE(wayland_keymap_cyrillic_layout) {
    Xkb xkb("ru");
    CHECK(xkb.state != nullptr);
    if (!xkb.state)
        return;

    // The ЙЦУКЕН 'о' is on the physical J key (evdev 36): it must produce
    // the Cyrillic letter, not act as Enter (the shifted-by-8 bug).
    const auto o = WaylandInput::translateKey(xkb.state, KeyJ, true);
    CHECK_EQ(o.key, 36);
    CHECK_EQ(o.text, std::string("\xD0\xBE")); // U+043E CYRILLIC SMALL LETTER O

    // Backspace still deletes under the Cyrillic layout.
    const auto bs = WaylandInput::translateKey(xkb.state, KeyBackspace, true);
    CHECK_EQ(bs.key, 14);
    CHECK(bs.text.empty());
}
