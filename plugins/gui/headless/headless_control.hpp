#pragma once

// Test/control API of the headless backend plugin. Link against the
// firmir-gui-headless target and set FIRMIR_GUI_PLUGIN_PATH to its
// directory so that Firmir::Gui::Platform dlopen()s the same library.

#include <cstdint>
#include <string>
#include <vector>

namespace Firmir {
namespace Gui {

struct Size;

namespace Headless {

/// Opaque window handle used by the control API.
using WindowHandle = void*;

} // namespace Headless
} // namespace Gui
} // namespace Firmir

extern "C" {

/// Adds a virtual screen (triggers Platform::signalScreenAdded).
void firmir_gui_headless_add_screen(const char* name, int width, int height);
/// Removes the screen named @p name (triggers signalScreenRemoved).
void firmir_gui_headless_remove_screen(const char* name);

/// Injects input events (delivered to the currently visible windows).
void firmir_gui_headless_inject_mouse(int action, float x, float y,
                                      int button);
void firmir_gui_headless_inject_key(bool pressed, int key, const char* text);
void firmir_gui_headless_inject_touch(int action, int id, float x, float y);

/// Number of windows created through the backend.
int firmir_gui_headless_window_count();
/// Copies the last presented frame of window @p index into @p outPixels
/// (ARGB32, outSize receives the pixel size). Returns false when the window
/// does not exist or no frame was presented yet.
bool firmir_gui_headless_last_frame(int index, std::uint32_t* outPixels,
                                    int* outWidth, int* outHeight);
/// Number of frames presented by window @p index so far.
int firmir_gui_headless_frame_count(int index);
}
