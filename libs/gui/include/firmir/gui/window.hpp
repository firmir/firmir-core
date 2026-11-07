#pragma once

#include <firmir/gui/events.hpp>
#include <firmir/object.hpp>

#include <memory>
#include <mutex>
#include <string>

namespace Firmir {
namespace Gui {

class Item;
class Screen;
class Window;

namespace detail {
std::recursive_mutex& sceneMutex(Window* window);
void scheduleRender(Window* window);
void clearFocusItem(Window* window, Item* item);
} // namespace detail

/**
 * @brief A window on a Screen, hosting a 2D item scene graph.
 *
 * The scene graph is mutated on the GUI thread and rendered on a dedicated
 * render thread owned by the window: update() schedules a frame, the render
 * thread rasterizes the item tree with the software Painter and hands the
 * ARGB32 buffer to the platform backend for presentation.
 *
 * Input routed by Platform is dispatched to the topmost enabled item under
 * the pointer (bubbling to ancestors until accepted); key events go to the
 * focus item.
 */
class Window : public Object {
public:
    explicit Window(Screen* screen, Object* parent = nullptr);
    ~Window() override;

    /// Owning screen; nullptr after the screen was unplugged.
    Screen* screen() const;

    std::string title() const;
    void setTitle(const std::string& title);

    /// Window geometry in screen coordinates.
    Rect geometry() const;
    void setGeometry(const Rect& geometry);
    void resize(int width, int height);

    bool isVisible() const;
    void show();
    void hide();
    void close() { hide(); }
    void showMaximized();
    void showFullscreen();
    void showMinimized();
    void showNormal();

    /// Root of the scene graph; reparent items to it to show them.
    Item* contentItem() const;

    /**
     * @brief Creates an item of type T under @p parent (usually contentItem()
     *        or another item), holding the scene mutex.
     *
     * Creating an item that is (transitively) attached to a shown window
     * races with the render thread unless the scene mutex is held for the
     * whole construction — use this factory (or Item::createChild) instead
     * of a bare `new T(parent)`.
     */
    template <typename T = Item, typename... Args>
    T* createItem(Object* parent, Args&&... args) {
        std::lock_guard<std::recursive_mutex> lock(detail::sceneMutex(this));
        return new T(parent, std::forward<Args>(args)...);
    }

    /// Destroys @p item (must belong to this window) under the scene mutex.
    void destroyItem(Item* item);

    /// Schedules a frame render. Thread-safe; coalesces requests.
    void update();

    /// The item receiving key events, or nullptr.
    Item* focusItem() const;
    void setFocusItem(Item* item);

    /// Signal "frameRendered" — emitted from the render thread after each
    /// presented frame.
    static constexpr auto signalFrameRendered = "frameRendered";
    /// Signal "visibilityChanged" — (bool visible), GUI thread.
    static constexpr auto signalVisibilityChanged = "visibilityChanged";

private:
    /// Input dispatch entry points used by Platform. GUI thread only.
    void dispatchMouse(MouseEvent& event);
    void dispatchTouch(TouchEvent& event);
    void dispatchKey(KeyEvent& event);

    /// Registered property adapters: Firml-generated code reads/writes the
    /// window geometry and title through the Object property system
    /// ("width"/"height" in pixels as double, "title" as std::string).
    double widthProperty() const;
    double heightProperty() const;
    void setWidthProperty(double width);
    void setHeightProperty(double height);
    std::string titleProperty() const;
    void setTitleProperty(std::string title);

    class Impl;
    std::unique_ptr<Impl> d;

    friend class Platform;
    friend std::recursive_mutex& detail::sceneMutex(Window*);
    friend void detail::scheduleRender(Window*);
    friend void detail::clearFocusItem(Window*, Item*);
};

} // namespace Gui
} // namespace Firmir
