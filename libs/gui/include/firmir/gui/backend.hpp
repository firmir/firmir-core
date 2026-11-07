#pragma once

// Plugin ABI for firmir-gui backends. The base library never links against
// backends: they are discovered as `firmir-gui-<name>.so` modules in the
// directories listed in the FIRMIR_GUI_PLUGIN_PATH environment variable
// (colon-separated) and selected with:
//   FIRMIR_GUI_DISPLAY — display backend name (default: "headless")
//   FIRMIR_GUI_INPUT   — input backend name (default: the display plugin's
//                        input factory when present, else "headless")
// A single .so may export both factory functions (e.g. a Wayland plugin
// providing display and input together).

#include <firmir/gui/events.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Firmir {
namespace Gui {

/// A physical or virtual screen exposed by a display backend.
class PlatformScreen {
public:
    virtual ~PlatformScreen() = default;
    virtual std::string name() const = 0;
    virtual Size size() const = 0;
};

/// A native window created by a display backend.
class PlatformWindow {
public:
    virtual ~PlatformWindow() = default;
    virtual void setTitle(const std::string& title) = 0;
    virtual void setGeometry(const Rect& geometry) = 0;
    virtual void setVisible(bool visible) = 0;
    virtual void setMaximized(bool maximized) { (void)maximized; }
    virtual void setFullscreen(bool fullscreen) { (void)fullscreen; }
    virtual void setMinimized() {}
    /// Native configure notification (Wayland/xdg, resize, WM actions).
    std::function<void(const Rect&)> geometryCallback;
    /// Presents a rendered ARGB32 frame. Called from the render thread;
    /// implementations must be thread-safe with respect to the GUI thread.
    virtual void present(const std::uint32_t* pixels, const Size& size) = 0;
};

/// Display backend: screen enumeration/hotplug and window factory.
class DisplayBackend {
public:
    virtual ~DisplayBackend() = default;
    /// Returns the currently connected screens (owned by the backend).
    virtual std::vector<PlatformScreen*> screens() = 0;
    /// Creates a native window on @p screen. The backend owns the window;
    /// release it with destroyWindow().
    virtual PlatformWindow* createWindow(PlatformScreen* screen) = 0;
    /// Destroys a window previously created with createWindow().
    virtual void destroyWindow(PlatformWindow* window) = 0;
    /// Invoked (from any thread) when a screen is plugged in or removed.
    std::function<void(PlatformScreen* screen, bool added)> screenCallback;
};

/// Input backend: keyboard/mouse/touch device events.
class InputBackend {
public:
    virtual ~InputBackend() = default;
    /// Invoked (from any thread) for every input event.
    std::function<void(const InputEvent& event)> eventCallback;
};

using CreateDisplayFunc = DisplayBackend* (*)();
using CreateInputFunc = InputBackend* (*)();

} // namespace Gui
} // namespace Firmir

/// Symbol a display plugin exports: `extern "C" Firmir::Gui::DisplayBackend*
/// firmir_gui_create_display()`. The returned object is owned by the plugin
/// (typically a singleton) and must outlive the Firmir::Gui::Platform
/// singleton.
#define FIRMIR_GUI_CREATE_DISPLAY "firmir_gui_create_display"
/// Symbol an input plugin exports: `extern "C" Firmir::Gui::InputBackend*
/// firmir_gui_create_input()`; same ownership rules as the display factory.
#define FIRMIR_GUI_CREATE_INPUT "firmir_gui_create_input"
