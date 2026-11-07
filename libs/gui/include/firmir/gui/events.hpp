#pragma once

#include <string>
#include <vector>

namespace Firmir {
namespace Gui {

/// 2D point in floating-point window/item coordinates.
struct Point {
    float x = 0;
    float y = 0;
};

/// Integer pixel size.
struct Size {
    int width = 0;
    int height = 0;
};

/// Integer rectangle (window geometry, clip regions).
struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool contains(float px, float py) const noexcept {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
    Rect intersected(const Rect& o) const noexcept {
        const int nx = x > o.x ? x : o.x;
        const int ny = y > o.y ? y : o.y;
        const int nx2 = (x + width < o.x + o.width) ? x + width : o.x + o.width;
        const int ny2 = (y + height < o.y + o.height) ? y + height : o.y + o.height;
        if (nx2 <= nx || ny2 <= ny)
            return {};
        return {nx, ny, nx2 - nx, ny2 - ny};
    }
};

/// Floating-point rectangle (item geometry).
struct RectF {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    bool contains(float px, float py) const noexcept {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

/// Mouse input event delivered to windows and items.
struct MouseEvent {
    enum class Action { Move, Press, Release, Wheel };
    Action action = Action::Move;
    Point pos;      ///< position in window coordinates
    int button = 0; ///< 0 = none, 1 = left, 2 = right, 3 = middle
    /// Wheel steps (Action::Wheel only): delta.y > 0 means the wheel rotated
    /// up (away from the user), delta.x > 0 means right; 1.0 = one detent.
    Point delta;
    bool accepted = false;
};

/// A single touch contact.
struct TouchPoint {
    int id = 0;
    Point pos;      ///< position in window coordinates
    bool pressed = false;
};

/// Touch input event delivered to windows and items.
struct TouchEvent {
    enum class Action { Begin, Update, End };
    Action action = Action::Begin;
    std::vector<TouchPoint> points;
    bool accepted = false;
};

/// Keyboard input event delivered to the focused item.
struct KeyEvent {
    bool pressed = false; ///< false = release
    int key = 0;          ///< evdev-style key code (linux/input-event-codes.h)
    std::string text;     ///< UTF-8 text when the key is printable
    bool accepted = false;
};

/// Raw input event emitted by input backend plugins.
struct InputEvent {
    enum class Type { Mouse, Key, Touch };
    Type type = Type::Mouse;
    MouseEvent mouse;   ///< valid when type == Mouse
    KeyEvent key;       ///< valid when type == Key
    TouchEvent touch;   ///< valid when type == Touch
};

} // namespace Gui
} // namespace Firmir
