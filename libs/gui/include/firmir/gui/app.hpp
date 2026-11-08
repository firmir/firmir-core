#pragma once

#include <firmir/object.hpp>
#include <firmir/string.hpp>

namespace Firmir {
namespace Gui {

class Window;

/**
 * @brief Application singleton exposed to Firml as "Firmir.Gui.App".
 *
 * Register an instance with Object::registerSingleton() before evaluating
 * generated handlers; destruction unregisters it automatically.
 */
class App : public Object {
public:
    explicit App(Window* window, Object* parent = nullptr);

    Window* window() const;
    void show();
    void hide();
    void quit();
    void setTitle(String title);

    /// Window state controls, exposed to Firml for window-control buttons.
    void showMinimized();
    void showNormal();
    void showMaximized();
    /// Toggles between maximized and normal (tracked locally).
    void toggleMaximized();
    void close();

    /// Primary screen size in pixels (0 when no screen is connected).
    /// Exposed to Firml as the read-only Number properties "screenWidth" and
    /// "screenHeight"; a propertyChanged is emitted on screen hotplug.
    double screenWidth() const;
    double screenHeight() const;

private:
    void rejectScreenSizeWrite(double); // the properties are read-only
    void notifyScreenSizeChanged();

    Window* m_window;
    bool m_maximized = false;
};

} // namespace Gui
} // namespace Firmir
