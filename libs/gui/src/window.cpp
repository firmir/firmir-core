#include <firmir/gui/window.hpp>

#include "detail/window_internal.hpp"

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
#include <mutex>

namespace Firmir {
namespace Gui {

namespace {

const Firmir::LoggingCategory& lcGuiWindow() {
    static const Firmir::LoggingCategory category("gui");
    return category;
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
            m_cv.wait(lock, [&] { return m_stop || m_framePending; });
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
    Impl(Window* q, Screen* screen) : q(q), screen(screen) {}

    void renderFrame();

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
};

void Window::Impl::renderFrame() {
    std::lock_guard<std::recursive_mutex> lock(sceneMutex);
    if (!visible || geometry.width <= 0 || geometry.height <= 0)
        return;
    if (!renderDevice) {
        const char* backendName = std::getenv("FIRMIR_RENDER_BACKEND");
        renderDevice = Render::Render::createDevice(
            backendName && *backendName ? backendName : "");
        if (!renderDevice) {
            if (!renderErrorLogged) {
                cLogWarning(lcGuiWindow())
                    << "no render backend (tried"
                                   << (backendName && *backendName
                                           ? backendName
                                           : "soft")
                                   << "); window content is not rendered";
                renderErrorLogged = true;
            }
            return;
        }
        renderResources = std::make_unique<RenderResources>(renderDevice.get());
        renderCommands.reset(renderDevice->createCommandBuffer());
    }
    if (!renderCommands)
        return;
    if (!renderTarget || renderTarget->size().width != geometry.width ||
        renderTarget->size().height != geometry.height) {
        Render::RenderTargetDesc desc;
        desc.width = geometry.width;
        desc.height = geometry.height;
        desc.colorFormat = Render::TextureFormat::Rgba8;
        renderTarget.reset(renderDevice->createRenderTarget(desc));
        if (!renderTarget)
            return;
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
    platformWindow->present(cpuTarget->pixels(),
                            Size{geometry.width, geometry.height});
    q->emitSignal(Window::signalFrameRendered);
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

void Window::update() { detail::scheduleRender(this); }

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

/// Clears the grabber/hover pointers when the pointed-at item dies (e.g. a
/// Repeater rebuild deleting the delegate under the cursor).
void Window::watchGrabbedItem(Item* item) {
    item->connect(this, Object::signalDestroyed, [this] {
        d->mouseGrabber = nullptr;
        d->hoverItem = nullptr;
    });
}

void Window::dispatchTouch(TouchEvent& event) {
    std::lock_guard<std::recursive_mutex> sceneLock(d->sceneMutex);
    if (event.points.empty() || !d->content)
        return;
    Item* target =
        d->content->itemAt(event.points[0].pos.x, event.points[0].pos.y);
    if (event.action == TouchEvent::Action::Begin && target &&
        (static_cast<int>(target->focusPolicy()) &
         static_cast<int>(FocusPolicy::ClickFocus)))
        setFocusItem(target);
    while (target && !event.accepted) {
        TouchEvent local = event;
        for (auto& point : local.points)
            point.pos = target->mapFromScene(point.pos);
        target->touchEvent(local);
        event.accepted = local.accepted;
        target = target->parentItem();
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

void scheduleRender(Window* window) {
    if (window->d->renderThread)
        window->d->renderThread->requestFrame();
}

void clearFocusItem(Window* window, Item* item) {
    if (window->d->focusItem == item)
        window->setFocusItem(nullptr);
}

} // namespace detail

} // namespace Gui
} // namespace Firmir

