#pragma once

// Internal helpers shared by item.cpp / window.cpp / platform.cpp.

#include <mutex>

namespace Firmir {
namespace Gui {

class Item;
class Window;

namespace detail {

/// Scene-graph mutex of the window (held while rendering and while items
/// are mutated). Recursive: nested mutations may re-enter it.
std::recursive_mutex& sceneMutex(Window* window);
/// Schedules a frame on the window's render thread (thread-safe).
void scheduleRender(Window* window);
/// Opens/closes a batch of scene updates. While a batch is open the render
/// threads are not woken: render requests are collected, and closing the
/// batch requests exactly one frame per changed window. The animation frame
/// driver uses this so all properties of one tick (e.g. a rectangle's x and
/// y) are rendered together instead of in alternating frames.
void beginFrameBatch();
void endFrameBatch();
/// Drops @p item as the window's focus item (item destruction).
void clearFocusItem(Window* window, Item* item);

} // namespace detail
} // namespace Gui
} // namespace Firmir
