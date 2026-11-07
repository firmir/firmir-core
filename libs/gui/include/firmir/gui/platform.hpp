#pragma once

#include <firmir/object.hpp>

#include <memory>
#include <vector>

namespace Firmir {
namespace Gui {

class PlatformWindow;
class Screen;
class Window;

/**
 * @brief Platform integration singleton: loads backend plugins and routes
 *        screen hotplug and input events.
 *
 * The display and input backends are dlopen()ed on first use, selected via
 * the FIRMIR_GUI_DISPLAY / FIRMIR_GUI_INPUT environment variables and found
 * in FIRMIR_GUI_PLUGIN_PATH (see backend.hpp).
 */
class Platform : public Object {
public:
    /// Returns the singleton, creating it (and loading plugins) on first
    /// call. Must first be called from the main/GUI thread.
    static Platform* instance();

    /// Currently connected screens.
    std::vector<Screen*> screens() const;
    /// Screen by name, or nullptr.
    Screen* screen(const std::string& name) const;

    /// Signal "screenAdded" — (Screen* screen), GUI thread.
    static constexpr auto signalScreenAdded = "screenAdded";
    /// Signal "screenRemoved" — (Screen* screen), emitted before the Screen
    /// object is destroyed, GUI thread.
    static constexpr auto signalScreenRemoved = "screenRemoved";

    bool event(Event& event) override;

private:
    Platform();

    /// Registers/unregisters a window for input routing. Called by Window.
    void addWindow(Window* window);
    void removeWindow(Window* window);
    /// Creates a native window on @p screen via the display backend.
    PlatformWindow* createPlatformWindow(Screen* screen);
    /// Destroys a native window created with createPlatformWindow().
    void destroyPlatformWindow(PlatformWindow* window);

    class Impl;
    std::unique_ptr<Impl> d;

    friend class Window;
};

} // namespace Gui
} // namespace Firmir
