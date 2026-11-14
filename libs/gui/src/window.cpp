#include <firmir/gui/window.hpp>

#include "detail/window_internal.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <firmir/gui/backend.hpp>
#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/theme.hpp>
#include <firmir/log.hpp>
#include <firmir/render/render.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>
#include <memory>
#include <mutex>

namespace Firmir {
namespace Gui {

namespace {

const Firmir::LoggingCategory& lcGuiWindow() {
    static const Firmir::LoggingCategory category("gui");
    return category;
}

/// FIRMIR_GUI_FRAME_TRACE=1 logs every frame's cadence (gap since the previous
/// frame, paint and present cost). Diagnostic for jerky animation and input
/// lag: an even gap with a paint time near the display refresh period is what
/// a smooth animation needs.
bool frameTraceEnabled() {
    static const bool enabled = [] {
        const char* value = std::getenv("FIRMIR_GUI_FRAME_TRACE");
        return value && *value && std::string(value) != "0";
    }();
    return enabled;
}

/// detail::sceneGuardFor implementation: locks the scene mutex of the window
/// the object belongs to (found by walking the parent chain up to an Item —
/// the object itself may be mid-destruction, when its own Item identity is
/// already gone, but its parents are still fully alive). Empty lock for
/// objects outside any GUI scene.
std::unique_lock<std::recursive_mutex> sceneGuardForObject(
    Firmir::Object* object) noexcept {
    for (Firmir::Object* p = object; p; p = p->parent())
        if (auto* item = dynamic_cast<Item*>(p))
            if (Window* w = item->window())
                return std::unique_lock<std::recursive_mutex>(
                    detail::sceneMutex(w));
    return {};
}

/// Frame batching vs. the render threads (see detail::beginFrameBatch).
///
/// A batch writes every property of one tick — x and y of a rectangle, say — but
/// each write takes and releases the scene mutex on its own. A frame painted in
/// between mirrors a half-applied tick (x from this tick, y from the previous
/// one): the staircase the batching exists to prevent.
///
/// So a batch only *counts as open*, and a render thread refuses to paint while
/// the count is not zero. The counting mutex is never held across a batch and
/// the GUI thread never waits here: it would otherwise wait for a whole paint
/// on every tick, and a paint of a fullscreen client buffer (copied in
/// WaylandGuiItem::paint) takes long enough to freeze the compositor — no
/// input, no frames. The render thread drops the scene lock while it waits (a
/// batch needs it for its writes) and re-reads the count after taking it back,
/// so a tick that starts later can only write after this frame is done.
std::mutex frameBatchMutex;
std::condition_variable frameBatchCv;
int openFrameBatches = 0;
/// Upper bound for a render thread waiting for an open batch. Nothing paints
/// half a tick while batches close in microseconds; the bound only keeps a
/// leaked batch (a callback that never returned) from freezing the window.
constexpr auto kFrameBatchWait = std::chrono::milliseconds(50);

/// True while an animation tick is writing its properties.
bool frameBatchOpen() {
    std::lock_guard<std::mutex> lock(frameBatchMutex);
    return openFrameBatches > 0;
}

/// Waits, bounded, for the open batches to close. Called without the scene lock.
void waitForFrameBatches() {
    std::unique_lock<std::mutex> lock(frameBatchMutex);
    frameBatchCv.wait_for(lock, kFrameBatchWait,
                          [] { return openFrameBatches == 0; });
}

/// Render worker: owns a dedicated OS thread; frames are rendered off the
/// GUI thread on request (Window::update()), coalescing duplicate requests.
class RenderThread : public Thread {
public:
    RenderThread() = default;

    void stop() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_one();
    }

    void requestFrame() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_framePending = true;
        }
        m_cv.notify_one();
    }

    std::function<void()> renderFrame; // set by Window before start()

protected:
    void run() override {
        std::unique_lock<std::mutex> lock(m_mutex);
        for (;;) {
            const auto idleStart = std::chrono::steady_clock::now();
            m_cv.wait(lock, [&] { return m_stop || m_framePending; });
            if (frameTraceEnabled()) {
                // No request for a whole frame period means the GUI thread
                // stalled (it ticks every 2 ms): report it here, since the
                // frame trace can only show the resulting gap.
                const std::chrono::duration<double, std::milli> idle =
                    std::chrono::steady_clock::now() - idleStart;
                if (idle > std::chrono::milliseconds(60))
                    cLogWarning(lcGuiWindow()) << "render thread idle"
                                               << idle.count() << "ms";
            }
            if (m_stop)
                break;
            m_framePending = false;
            lock.unlock();
            renderFrame();
            lock.lock();
        }
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_stop = false;
    bool m_framePending = false;
};

/// Event type for platform-originated geometry changes (see Window::event).
constexpr int GeometryEventType = Event::User + 103;

/// Posted to the window when the platform backend reports a new size from a
/// backend thread (e.g. the Wayland dispatch thread on xdg_toplevel
/// configure). Applying it inline would take the scene mutex while the
/// backend holds its api lock — a lock-order inversion against the render
/// thread (scene mutex held, api wanted in present()), i.e. a deadlock.
class GeometryEvent : public Event {
public:
    explicit GeometryEvent(const Rect& geometry)
        : Event(GeometryEventType), geometry(geometry) {}
    Rect geometry;
};

} // namespace

class Window::Impl {
public:
    Impl(Window* q, Screen* screen) : q(q), screen(screen) {
        // FIRMIR_GUI_KEEP_WARM_MS: see keepWarmMs.
        if (const char* value = std::getenv("FIRMIR_GUI_KEEP_WARM_MS"))
            keepWarmMs = std::max(0, std::atoi(value));
    }

    void renderFrame();
    void keepWarmTick();

    Window* q;
    Screen* screen;
    PlatformWindow* platformWindow = nullptr;
    Item* content = nullptr;
    RenderThread* renderThread = nullptr;
    // Guards the item tree and the window state read by renderFrame().
    // Recursive: item setters lock it while e.g. Window::setGeometry()
    // already holds it.
    std::recursive_mutex sceneMutex;
    // Render stack (render thread only): the scene is drawn through the
    // firmir-render API; the backend is the first loadable of
    // vulkan/opengl/soft, overridable via FIRMIR_RENDER_BACKEND.
    std::unique_ptr<Render::RenderDevice> renderDevice;
    std::unique_ptr<RenderResources> renderResources;
    std::unique_ptr<Render::RenderCommandBuffer> renderCommands;
    std::unique_ptr<Render::RenderTarget> renderTarget;
    bool renderErrorLogged = false;
    std::string title;
    Rect geometry;
    bool visible = false;
    Item* focusItem = nullptr;
    Item* hoverItem = nullptr;   // deepest item under the cursor (moves)
    Item* mouseGrabber = nullptr; // item that accepted the current press
    Item* touchGrabber = nullptr; // item that accepted the current contact
    int touchGrabberId = 0;       // TouchPoint::id of that contact
    /// FIRMIR_GUI_KEEP_WARM_MS (0 = off): after the last change, keep requesting
    /// frames for this long. Rendering on demand lets the CPU and the GPU drop
    /// to their idle clocks, and the first frames of the next burst then run
    /// 2-3x slower (tablet panel: present 45 -> 16 ms, paint 0.6 -> 0.12 ms), so
    /// an animation that starts after a pause steps in big chunks. Presenting the
    /// unchanged scene for a moment after the last change keeps the pipeline and
    /// the clocks up; it costs a few frames of idle work.
    int keepWarmMs = 0;
    std::unique_ptr<Timer> keepWarmTimer;
    std::chrono::steady_clock::time_point lastUpdate{};
};

void Window::Impl::renderFrame() {
    const auto frameStart = std::chrono::steady_clock::now();
    // Only painting takes the scene lock (the tree must not change while it is
    // walked). present() — texture upload, page flip and the vsync wait of the
    // KMS backend — touches no scene state and is the slowest part of a frame,
    // so it runs unlocked: holding the lock across it made the GUI thread
    // (input dispatch, animation ticks) wait for a whole display refresh per
    // event, which starved input and bunched the animation samples.
    Render::CpuRenderTarget* target = nullptr;
    PlatformWindow* native = nullptr;
    Size targetSize;
    std::chrono::steady_clock::time_point sceneAcquired = frameStart;
    {
        std::unique_lock<std::recursive_mutex> lock(sceneMutex);
        // Do not paint a scene an animation tick is still writing (see
        // frameBatchOpen). The scene lock is dropped while waiting — the batch
        // needs it for its writes — and the count is re-read after taking it
        // back, so a tick that starts later can only write after this frame.
        // Bounded, but generous: a real tick closes in microseconds, so the
        // wait ends on the first attempt; only a leaked batch (a callback that
        // never returned) burns the budget, and even then the window paints.
        for (int attempt = 0; attempt < 16 && frameBatchOpen(); ++attempt) {
            lock.unlock();
            waitForFrameBatches();
            lock.lock();
        }
        sceneAcquired = std::chrono::steady_clock::now();
        if (!visible || geometry.width <= 0 || geometry.height <= 0)
            return;
        native = platformWindow;
        if (!renderDevice) {
            const char* backendName = std::getenv("FIRMIR_RENDER_BACKEND");
            renderDevice = Render::Render::createDevice(
                backendName && *backendName ? backendName : "");
            if (!renderDevice) {
                if (!renderErrorLogged) {
                    const std::string requested =
                        backendName && *backendName ? backendName : "default";
                    cLogWarning(lcGuiWindow())
                        << ("no render backend available (" + requested +
                            "); window content is not rendered");
                    renderErrorLogged = true;
                }
                return;
            }
            renderResources =
                std::make_unique<RenderResources>(renderDevice.get());
            renderCommands.reset(renderDevice->createCommandBuffer());
        }
        if (!renderCommands) {
            if (!renderErrorLogged) {
                cLogWarning(lcGuiWindow())
                    << "render backend" << renderDevice->backendName()
                    << "cannot create a command buffer; window content is not "
                       "rendered";
                renderErrorLogged = true;
            }
            return;
        }
        if (!renderTarget || renderTarget->size().width != geometry.width ||
            renderTarget->size().height != geometry.height) {
            Render::RenderTargetDesc desc;
            desc.width = geometry.width;
            desc.height = geometry.height;
            desc.colorFormat = Render::TextureFormat::Rgba8;
            renderTarget.reset(renderDevice->createRenderTarget(desc));
            if (!renderTarget) {
                if (!renderErrorLogged) {
                    cLogWarning(lcGuiWindow())
                        << "render backend" << renderDevice->backendName()
                        << "cannot create a" << desc.width << "x" << desc.height
                        << "render target; window content is not rendered";
                    renderErrorLogged = true;
                }
                return;
            }
        }
        auto* cpuTarget =
            dynamic_cast<Render::CpuRenderTarget*>(renderTarget.get());
        if (!cpuTarget) {
            if (!renderErrorLogged) {
                cLogWarning(lcGuiWindow())
                    << "render backend" << renderDevice->backendName()
                    << "has no CPU-readable targets; window content is not "
                       "rendered";
                renderErrorLogged = true;
            }
            return;
        }

        renderCommands->begin();
        Render::ClearValue clear; // opaque black
        clear.r = clear.g = clear.b = 0;
        clear.a = 1;
        renderCommands->beginRenderPass(renderTarget.get(), clear);
        {
            Painter painter(*renderResources, renderCommands.get(),
                            renderTarget.get());
            if (content)
                content->render(painter);
        }
        renderCommands->endRenderPass();
        renderCommands->end();
        renderDevice->submit(renderCommands.get());
        target = cpuTarget;
        targetSize = Size{geometry.width, geometry.height};
    }
    const auto painted = std::chrono::steady_clock::now();
    if (native && target)
        native->present(target->pixels(), targetSize);
    const auto presented = std::chrono::steady_clock::now();
    {
        // Emitted under the lock, as before: handlers are Direct and may read
        // the scene (tests sample the animated geometry here).
        std::lock_guard<std::recursive_mutex> lock(sceneMutex);
        q->emitSignal(Window::signalFrameRendered);
    }
    if (frameTraceEnabled()) {
        static thread_local auto previousStart = frameStart;
        const auto span = [](auto from, auto to) {
            return std::chrono::duration<double, std::milli>(to - from).count();
        };
        cLogInfo(lcGuiWindow())
            << "frame gap" << span(previousStart, frameStart) << "wait"
            << span(frameStart, sceneAcquired) << "paint"
            << span(sceneAcquired, painted) << "present"
            << span(painted, presented) << "(" << targetSize.width << "x"
            << targetSize.height << ")";
        previousStart = frameStart;
    }
}

Window::Window(Screen* screen, Object* parent)
    : Object(parent), d(std::make_unique<Impl>(this, screen)) {
    // Route the core object-tree teardown/reparenting through the scene
    // mutex so the render thread never observes a mutating tree.
    Firmir::detail::sceneGuardFor = &sceneGuardForObject;
    d->content = new Item(this);
    // Firml-facing properties (bindings/handlers go through setProperty).
    registerProperty<double>("width", &Window::widthProperty,
                             &Window::setWidthProperty);
    registerProperty<double>("height", &Window::heightProperty,
                             &Window::setHeightProperty);
    registerProperty<std::string>("title", &Window::titleProperty,
                                  &Window::setTitleProperty);
    Platform::instance()->addWindow(this);
    if (screen) {
        screen->connect(this, Screen::signalRemoved, [this] {
            hide();
            d->screen = nullptr;
        });
    }
    // Controls resolve their colors/metrics from the Theme singleton at paint
    // time; a theme change must repaint the whole window.
    Theme::instance()->connect<std::string, std::any>(
        this, Theme::signalPropertyChanged,
        [this](const std::string&, const std::any&) {
            if (d->content)
                d->content->update();
        },
        ConnectionMode::Queued);
    d->renderThread = new RenderThread;
    d->renderThread->renderFrame = [this] { d->renderFrame(); };
    d->renderThread->start();
    // The platform window is created lazily on first show().
}

Window::~Window() {
    detail::forgetBatchedWindow(this);
    Platform::instance()->removeWindow(this);
    d->renderThread->stop();
    d->renderThread->quit();
    d->renderThread->wait();
    delete d->renderThread;
    Platform::instance()->destroyPlatformWindow(d->platformWindow);
}
Screen* Window::screen() const { return d->screen; }

std::string Window::title() const { return d->title; }

void Window::setTitle(const std::string& title) {
    if (d->title == title)
        return;
    d->title = title;
    if (d->platformWindow)
        d->platformWindow->setTitle(title);
    notifyPropertyChanged("title");
}

double Window::widthProperty() const { return d->geometry.width; }
double Window::heightProperty() const { return d->geometry.height; }

void Window::setWidthProperty(double width) {
    resize(static_cast<int>(width), d->geometry.height); // setGeometry notifies
}

void Window::setHeightProperty(double height) {
    resize(d->geometry.width, static_cast<int>(height)); // setGeometry notifies
}

std::string Window::titleProperty() const { return d->title; }
void Window::setTitleProperty(std::string title) { setTitle(title); }

Rect Window::geometry() const { return d->geometry; }

bool Window::event(Event& event) {
    if (event.type() == GeometryEventType) {
        setGeometry(static_cast<GeometryEvent&>(event).geometry);
        return true;
    }
    return Object::event(event);
}

void Window::setGeometry(const Rect& geometry) {
    std::lock_guard<std::recursive_mutex> lock(d->sceneMutex);
    const bool widthChanged = geometry.width != d->geometry.width;
    const bool heightChanged = geometry.height != d->geometry.height;
    d->geometry = geometry;
    // The content item always covers the whole window.
    d->content->setGeometry(0, 0, geometry.width, geometry.height);
    if (d->platformWindow)
        d->platformWindow->setGeometry(geometry);
    update();
    // Firml bindings on the window size re-evaluate through these.
    if (widthChanged)
        notifyPropertyChanged("width");
    if (heightChanged)
        notifyPropertyChanged("height");
}

void Window::resize(int width, int height) {
    Rect r = d->geometry;
    r.width = width;
    r.height = height;
    setGeometry(r);
}

bool Window::isVisible() const { return d->visible; }

void Window::show() {
    if (d->visible)
        return;
    if (!d->screen)
        return; // the screen is gone (hot-unplugged)
    if (!d->platformWindow)
        d->platformWindow =
            Platform::instance()->createPlatformWindow(d->screen);
    if (!d->platformWindow)
        return;
    d->platformWindow->geometryCallback = [this](const Rect& geometry) {
        // Backend threads must not block on the scene mutex (see
        // GeometryEvent); the GUI thread applies the size directly.
        if (Thread::currentThread() == thread())
            setGeometry(geometry);
        else
            postEvent(this, std::make_unique<GeometryEvent>(geometry));
    };
    d->platformWindow->setTitle(d->title);
    d->platformWindow->setGeometry(d->geometry);
    d->platformWindow->setVisible(true);
    {
        std::lock_guard<std::recursive_mutex> lock(d->sceneMutex);
        d->visible = true;
    }
    if (d->focusItem)
        d->focusItem->notifyPropertyChanged("activeFocus");
    emitSignal(signalVisibilityChanged, true);
    update();
}

void Window::hide() {
    if (!d->visible)
        return;
    if (d->keepWarmTimer)
        d->keepWarmTimer->stop();
    {
        std::lock_guard<std::recursive_mutex> lock(d->sceneMutex);
        d->visible = false;
    }
    if (d->platformWindow)
        d->platformWindow->setVisible(false);
    if (d->focusItem)
        d->focusItem->notifyPropertyChanged("activeFocus");
    emitSignal(signalVisibilityChanged, false);
}

void Window::showMaximized() {
    if (!d->platformWindow) show();
    if (d->platformWindow) d->platformWindow->setMaximized(true);
}
void Window::showFullscreen() {
    if (!d->platformWindow) show();
    if (d->platformWindow) d->platformWindow->setFullscreen(true);
}
void Window::showMinimized() {
    if (d->platformWindow) d->platformWindow->setMinimized();
}
void Window::showNormal() {
    if (d->platformWindow) {
        d->platformWindow->setFullscreen(false);
        d->platformWindow->setMaximized(false);
    }
}

Item* Window::contentItem() const { return d->content; }

void Window::destroyItem(Item* item) {
    if (!item || item->window() != this)
        return;
    std::lock_guard<std::recursive_mutex> lock(d->sceneMutex);
    delete item;
}

void Window::update() {
    if (d->keepWarmMs > 0) {
        d->lastUpdate = std::chrono::steady_clock::now();
        if (!d->keepWarmTimer) {
            // No parent: the Impl owns it, so it dies with the window and never
            // outlives the render thread it wakes.
            d->keepWarmTimer = std::make_unique<Timer>();
            d->keepWarmTimer->setInterval(std::chrono::milliseconds(16));
            d->keepWarmTimer->connect(d->keepWarmTimer.get(), "timeout",
                                      [impl = d.get()] { impl->keepWarmTick(); },
                                      ConnectionMode::Direct);
        }
        if (!d->keepWarmTimer->isActive())
            d->keepWarmTimer->start();
    }
    detail::scheduleRender(this);
}

void Window::Impl::keepWarmTick() {
    const auto now = std::chrono::steady_clock::now();
    if (now - lastUpdate >= std::chrono::milliseconds(keepWarmMs)) {
        if (keepWarmTimer)
            keepWarmTimer->stop();
        return;
    }
    if (renderThread)
        renderThread->requestFrame();
}

Item* Window::focusItem() const { return d->focusItem; }

void Window::setFocusItem(Item* item) {
    std::lock_guard<std::recursive_mutex> sceneLock(d->sceneMutex);
    if (d->focusItem == item)
        return;
    Item* previous = d->focusItem;
    if (previous) {
        previous->focusOutEvent();
        previous->notifyPropertyChanged("focus");
        previous->notifyPropertyChanged("activeFocus");
    }
    d->focusItem = item;
    if (item) {
        item->focusInEvent();
        item->notifyPropertyChanged("focus");
        item->notifyPropertyChanged("activeFocus");
    }
}

void Window::dispatchMouse(MouseEvent& event) {
    std::lock_guard<std::recursive_mutex> sceneLock(d->sceneMutex);
    Item* target =
        d->content ? d->content->itemAt(event.pos.x, event.pos.y) : nullptr;

    // While a button is held, the item that accepted the press grabs the
    // pointer: moves and the release go to it (drag semantics) even when
    // the cursor leaves its geometry.
    if (event.action != MouseEvent::Action::Press && d->mouseGrabber)
        target = d->mouseGrabber;

    // Hover tracking: the item under the cursor changes on moves; the
    // previous one gets one last move event (its position is outside its
    // bounds) so hoverEnabled MouseAreas clear containsMouse and emit
    // exited() instead of staying hovered forever.
    if (event.action == MouseEvent::Action::Move && !d->mouseGrabber &&
        target != d->hoverItem) {
        if (d->hoverItem) {
            MouseEvent leave = event;
            leave.pos = d->hoverItem->mapFromScene(event.pos);
            d->hoverItem->mouseEvent(leave);
        }
        d->hoverItem = target;
        if (d->hoverItem)
            watchGrabbedItem(d->hoverItem); // clears both pointers on death
    }

    // Click-to-focus: the deepest hit item is often a non-focusable
    // decoration child (e.g. the Text inside an EditableText) — walk up to
    // the nearest ancestor that accepts click focus.
    if (event.action == MouseEvent::Action::Press) {
        for (Item* focusable = target; focusable;
             focusable = focusable->parentItem()) {
            if (static_cast<int>(focusable->focusPolicy()) &
                static_cast<int>(FocusPolicy::ClickFocus)) {
                setFocusItem(focusable);
                break;
            }
        }
    }
    Item* acceptedBy = nullptr;
    while (target && !event.accepted) {
        MouseEvent local = event;
        local.pos = target->mapFromScene(event.pos);
        target->mouseEvent(local);
        event.accepted = local.accepted;
        acceptedBy = event.accepted ? target : nullptr;
        target = target->parentItem();
    }
    if (event.action == MouseEvent::Action::Press) {
        d->mouseGrabber = acceptedBy;
        if (d->mouseGrabber)
            watchGrabbedItem(d->mouseGrabber);
    } else if (event.action == MouseEvent::Action::Release) {
        d->mouseGrabber = nullptr;
    }
}

/// Clears the grabbers/hover pointer when the pointed-at item dies (e.g. a
/// Repeater rebuild deleting the delegate under the cursor, or the button
/// itself going away mid-drag).
void Window::watchGrabbedItem(Item* item) {
    item->connect(this, Object::signalDestroyed, [this, item] {
        if (d->mouseGrabber == item)
            d->mouseGrabber = nullptr;
        if (d->hoverItem == item)
            d->hoverItem = nullptr;
        if (d->touchGrabber == item)
            d->touchGrabber = nullptr;
    });
}

void Window::dispatchTouch(TouchEvent& event) {
    std::lock_guard<std::recursive_mutex> sceneLock(d->sceneMutex);
    if (event.points.empty() || !d->content)
        return;
    const int id = event.points.front().id;
    // Same grab semantics as the mouse: the item that accepts Begin keeps
    // receiving this contact (Update/End) after the finger leaves its
    // geometry, so dragging off a button and releasing there still clears
    // its pressed state instead of leaving it stuck down.
    const bool grabbed =
        d->touchGrabber && d->touchGrabberId == id;
    Item* target = nullptr;
    if (event.action != TouchEvent::Action::Begin && grabbed)
        target = d->touchGrabber;
    else
        target = d->content->itemAt(event.points.front().pos.x,
                                    event.points.front().pos.y);
    if (event.action == TouchEvent::Action::Begin && target &&
        (static_cast<int>(target->focusPolicy()) &
         static_cast<int>(FocusPolicy::ClickFocus)))
        setFocusItem(target);
    Item* acceptedBy = nullptr;
    while (target && !event.accepted) {
        TouchEvent local = event;
        for (auto& point : local.points)
            point.pos = target->mapFromScene(point.pos);
        target->touchEvent(local);
        event.accepted = local.accepted;
        acceptedBy = event.accepted ? target : nullptr;
        target = target->parentItem();
    }
    // A second finger (different id) leaves the current grab alone: its
    // contact is hit-tested on its own, and the grabbed one keeps its End.
    if (event.action == TouchEvent::Action::Begin) {
        if (!d->touchGrabber) {
            d->touchGrabber = acceptedBy;
            d->touchGrabberId = id;
            if (d->touchGrabber)
                watchGrabbedItem(d->touchGrabber);
        }
    } else if (event.action == TouchEvent::Action::End && grabbed) {
        d->touchGrabber = nullptr;
    }
}

void Window::dispatchKey(KeyEvent& event) {
    std::lock_guard<std::recursive_mutex> sceneLock(d->sceneMutex);
    // KEY_TAB (evdev 15): cycle through activeFocusOnTab items.
    if (event.pressed && event.key == 15) {
        std::vector<Item*> chain;
        std::function<void(Item*)> collect = [&](Item* item) {
            if (!item)
                return;
            if (item->activeFocusOnTab() && item->isVisible() &&
                item->isEnabled() &&
                (static_cast<int>(item->focusPolicy()) &
                 static_cast<int>(FocusPolicy::TabFocus)))
                chain.push_back(item);
            for (Item* child : item->childItems())
                collect(child);
        };
        collect(d->content);
        if (!chain.empty()) {
            std::size_t index = 0;
            for (std::size_t i = 0; i < chain.size(); ++i)
                if (chain[i] == d->focusItem) {
                    index = (i + 1) % chain.size();
                    break;
                }
            setFocusItem(chain[index]);
            event.accepted = true;
            return;
        }
    }
    Item* target = d->focusItem;
    while (target && !event.accepted) {
        target->keyEvent(event);
        target = target->parentItem();
    }
}

namespace detail {

std::recursive_mutex& sceneMutex(Window* window) {
    return window->d->sceneMutex;
}

namespace {

// Frame batching (see beginFrameBatch): a thread-local depth counter plus the
// windows that asked for a frame while the batch was open.
thread_local int frameBatchDepth = 0;
thread_local std::vector<Window*> frameBatchWindows;

} // namespace

void beginFrameBatch() {
    // Only counted, never locked across the batch: the GUI thread must not wait
    // for a render thread (see frameBatchOpen).
    if (frameBatchDepth++ == 0) {
        std::lock_guard<std::mutex> lock(frameBatchMutex);
        ++openFrameBatches;
    }
}

void endFrameBatch() {
    if (frameBatchDepth == 0 || --frameBatchDepth > 0)
        return;
    {
        std::lock_guard<std::mutex> lock(frameBatchMutex);
        --openFrameBatches;
    }
    frameBatchCv.notify_all();
    std::vector<Window*> windows;
    windows.swap(frameBatchWindows);
    for (Window* window : windows)
        if (window->d->renderThread)
            window->d->renderThread->requestFrame();
}

void forgetBatchedWindow(Window* window) {
    frameBatchWindows.erase(
        std::remove(frameBatchWindows.begin(), frameBatchWindows.end(), window),
        frameBatchWindows.end());
}

void scheduleRender(Window* window) {
    if (!window->d->renderThread)
        return;
    if (frameBatchDepth > 0) {
        // Collect instead of waking the render thread; the batch guarantees
        // that every property written during this tick is in the frame.
        if (std::find(frameBatchWindows.begin(), frameBatchWindows.end(),
                      window) == frameBatchWindows.end())
            frameBatchWindows.push_back(window);
        return;
    }
    window->d->renderThread->requestFrame();
}

void clearFocusItem(Window* window, Item* item) {
    if (window->d->focusItem == item)
        window->setFocusItem(nullptr);
}

} // namespace detail

} // namespace Gui
} // namespace Firmir

