#pragma once

// wl_keyboard -> Firmir key event translation, shared between the Wayland
// GUI plugin and its unit test.
//
// wl_keyboard delivers evdev keycodes on the wire (the +8 offset to XKB
// keycodes is a client-side convention: xkbcommon indexes by XKB keycodes).
// Getting the offset wrong shifts every key by one row: Backspace (evdev
// 14) is misread as KEY_5 and types "5", the ЙЦУКЕН 'о' position (KEY_J,
// 36) is misread as KEY_ENTER and starts a new line.

#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <string>

namespace Firmir {
namespace WaylandInput {

/// Canonical evdev key code for editing/navigation keysyms, -1 for
/// printable/unknown keys.
inline int editingKeyCode(xkb_keysym_t keysym) {
    switch (keysym) {
    case XKB_KEY_BackSpace: return 14;  // KEY_BACKSPACE
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: return 28;   // KEY_ENTER
    case XKB_KEY_Tab:
    case XKB_KEY_KP_Tab:
    case XKB_KEY_ISO_Left_Tab: return 15; // KEY_TAB
    case XKB_KEY_Escape: return 1;      // KEY_ESC
    case XKB_KEY_Delete: return 111;    // KEY_DELETE
    case XKB_KEY_Home: return 102;      // KEY_HOME
    case XKB_KEY_End: return 107;       // KEY_END
    case XKB_KEY_Left: return 105;      // KEY_LEFT
    case XKB_KEY_Right: return 106;     // KEY_RIGHT
    case XKB_KEY_Up: return 103;        // KEY_UP
    case XKB_KEY_Down: return 108;      // KEY_DOWN
    case XKB_KEY_Prior: return 104;     // KEY_PAGE_UP
    case XKB_KEY_Next: return 109;      // KEY_PAGE_DOWN
    default: return -1;
    }
}

struct KeyTranslation {
    int key;         // evdev code (normalized for editing/navigation keys)
    std::string text; // printable UTF-8 text (press only), may be empty
};

/// Translates a wl_keyboard key code into Firmir InputEvent fields.
/// @p wlKey is the wire value (evdev numbering); xkb lookups use wlKey + 8.
/// Editing/navigation keys are normalized by keysym so a layout that puts a
/// printable character on an unusual position can never turn Backspace into
/// text; text is produced on press only.
inline KeyTranslation translateKey(xkb_state* state, std::uint32_t wlKey,
                                   bool pressed) {
    KeyTranslation out{static_cast<int>(wlKey), {}};
    if (!state)
        return out;
    const xkb_keycode_t xkbKey = wlKey + 8; // xkb keycode = evdev + 8
    const xkb_keysym_t keysym = xkb_state_key_get_one_sym(state, xkbKey);
    if (const int editing = editingKeyCode(keysym); editing >= 0) {
        out.key = editing;
        return out;
    }
    if (pressed) {
        char buf[32];
        const int len =
            xkb_state_key_get_utf8(state, xkbKey, buf, sizeof(buf) - 1);
        if (len > 0) {
            buf[len] = '\0';
            // C0 controls (Enter/Tab/Backspace) are handled by key code.
            if (static_cast<unsigned char>(buf[0]) >= 0x20 || len > 1)
                out.text = buf;
        }
    }
    return out;
}

} // namespace WaylandInput
} // namespace Firmir
