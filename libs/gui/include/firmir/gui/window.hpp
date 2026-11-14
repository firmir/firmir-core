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
void beginFrameBatch();
void endFrameBatch();
void forgetBatchedWindow(Window* window);
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

    /**
     * @brief Moves the window to @p screen (nullptr detaches it).
     *
     * A native window belongs to one screen, so it is re-created there; the
     * item tree, the focus and running animations stay. DPI-dependent layout
     * re-evaluates through signalScreenChanged (bindings on Window.ppi/ppcm),
     * and a visible window is shown on the new screen.
     */
    void setScreen(Screen* screen);

    /**
     * @brief Pixel density of the window's screen (Screen::ppi()/ppcm()).
     *
     * 0 when there is no screen or the backend does not report the physical
     * panel size. Every window has its own screen, so density belongs here
     * rather than to the application singleton: two windows of one app can sit
     * on two panels with different densities.
     *
     * Exposed to Firml as the read-only Number properties "ppi" and "ppcm" of
     * the window, and re-emitted on screen hotplug and on setScreen(), so a
     * layout can be sized in millimetres:
     *
     *     property number mm: root.ppcm > 0 ? root.ppcm / 10 : 3.7795
     *     width: 9 * mm      // a finger-sized touch target
     *
     * (ppcm is pixels per centimetre, so a tenth of it is one millimetre;
     * 3.7795 px/mm is 96 ppi, the density that fixed pixel sizes assume.)
     */
    double ppi() const;
    double ppcm() const;

    /**
     * @brief How much of its screen the window covers, 0..1.
     *
     * min(width / screen width, height / screen height); 1 when the window
     * fills the screen or has none. Millimetre metrics describe a fullscreen
     * window, so multiply them by this factor when the window only takes a
     * part of the screen and the layout should keep its proportions instead
     * of keeping the physical size:
     *
     *     property number mm: (root.ppcm > 0 ? root.ppcm / 10 : 3.7795) * root.uiScale
     *
     * Read-only Number property "uiScale", re-emitted when the window is
     * resized and when it moves to another screen.
     */
    double uiScale() const;

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
    /// Signal "screenChanged" — (Screen* screen, nullptr when the screen went
    /// away), GUI thread. Emitted by setScreen() and when the window's screen
    /// is unplugged; re-evaluate DPI-dependent layout here.
    static constexpr auto signalScreenChanged = "screenChanged";
    /// Signal "visibilityChanged" — (bool visible), GUI thread.
    static constexpr auto signalVisibilityChanged = "visibilityChanged";

private:
    /// Handler of the current screen's signalRemoved: the screen object is
    /// already going away, so the window only drops it.
    void screenRemoved();

    /// Re-emits "ppi"/"ppcm"/"uiScale" (Firml bindings on the metrics
    /// re-evaluate).
    void notifyDensityChanged();
    /// The density/scale properties are read-only.
    void rejectMetricWrite(double value);

    /// Input dispatch entry points used by Platform. GUI thread only.
    void dispatchMouse(MouseEvent& event);
    void watchGrabbedItem(Item* item);
    void dispatchTouch(TouchEvent& event);
    void dispatchKey(KeyEvent& event);

    /// Handles events posted from backend threads (geometry changes arrive
    /// as GeometryEvent so the backend never blocks on the scene mutex).
    bool event(Event& event) override;

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
    friend void detail::endFrameBatch();
    friend void detail::clearFocusItem(Window*, Item*);
};

} // namespace Gui
} // namespace Firmir
