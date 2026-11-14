#include <firmir/gui/platform.hpp>

#include <algorithm>
#include <cstdlib>
#include <firmir/detail/dynload.hpp>
#include <firmir/gui/detail/backendselection.hpp>
#include <firmir/thread.hpp>
#include <firmir/gui/backend.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/log.hpp>
#include <mutex>
#include <stdexcept>
#ifndef _WIN32
#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#endif

namespace Firmir {
namespace Gui {

namespace {

const LoggingCategory& lcGui() {
    static const LoggingCategory category("gui");
    return category;
}

enum { ScreenChangedEventType = Event::User + 101,
       InputEventType = Event::User + 102 };

/// Posted to Platform when a backend reports screen hotplug off-thread.
class ScreenChangedEvent : public Event {
public:
    ScreenChangedEvent(PlatformScreen* screen, bool added)
        : Event(ScreenChangedEventType), screen(screen), added(added) {}
    PlatformScreen* screen;
    bool added;
};

/// Posted to Platform when an input backend reports an event off-thread.
class PlatformInputEvent : public Event {
public:
    explicit PlatformInputEvent(const InputEvent& input)
        : Event(InputEventType), input(input) {}
    InputEvent input;
};

/// Directories searched for firmir-gui-<name> plugins (.so/.dll):
/// FIRMIR_GUI_PLUGIN_PATH (separator: ':' on Linux, ';' on Windows), then
/// directories relative to the running executable (build tree and installed
/// layouts).
std::vector<std::string> pluginDirs() {
    std::vector<std::string> dirs = Firmir::detail::pluginSearchDirs(
        "FIRMIR_GUI_PLUGIN_PATH",
        {"../plugins", "../../plugins", "../../../plugins", "plugins"});
#ifdef FIRMIR_SYSTEM_PLUGIN_DIR
    dirs.push_back(FIRMIR_SYSTEM_PLUGIN_DIR);
#endif
    return dirs;
}

#ifndef _WIN32
/// True when a Wayland compositor accepts connections: $WAYLAND_DISPLAY
/// (absolute, or relative to $XDG_RUNTIME_DIR; default wayland-0) must exist
/// and be connectable. A stale socket file counts as "no compositor" so the
/// next backend of the list is tried instead of failing with "no screens".
bool waylandCompositorAvailable() {
    const char* display = std::getenv("WAYLAND_DISPLAY");
    std::string socket = display && *display ? display : "";
    if (socket.empty() || socket.front() != '/') {
        const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
        if (!runtimeDir || !*runtimeDir)
            return false;
        socket = std::string(runtimeDir) + "/" +
                 (socket.empty() ? std::string("wayland-0") : socket);
    }
    sockaddr_un address{};
    if (socket.size() >= sizeof(address.sun_path))
        return false;
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket.c_str(), socket.size() + 1);
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return false;
    const bool connected =
        ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0;
    ::close(fd);
    return connected;
}

/// True when at least one /dev/dri/cardN device node exists.
bool drmDeviceExists() {
    for (int i = 0; i < 16; ++i) {
        const std::string node = "/dev/dri/card" + std::to_string(i);
        if (::access(node.c_str(), F_OK) == 0)
            return true;
    }
    return false;
}
#endif

void* loadPlugin(const char* kind, const std::string& name) {
    const std::string file =
        "firmir-gui-" + name + Firmir::detail::sharedLibrarySuffix();
    for (const std::string& dir : pluginDirs()) {
        const std::string path = dir + "/" + file;
        if (void* handle = Firmir::detail::libraryOpen(path)) {
            cLogInfo(lcGui()) << "loaded" << kind << "backend" << path;
            return handle;
        }
    }
    return nullptr;
}

} // namespace

class Platform::Impl {
public:
    void loadBackends(Platform* q);
    void handleScreenChanged(Platform* q, PlatformScreen* backendScreen,
                             bool added);
    void routeInput(Platform* q, const InputEvent& event);
    PlatformScreen* backendFor(Screen* screen) const;
    /// Tells the input backend the size of the primary screen (absolute
    /// devices such as evdev touchscreens are scaled to it).
    void notifyInputScreenSize();

    void* displayHandle = nullptr;
    void* inputHandle = nullptr;
    DisplayBackend* display = nullptr;
    InputBackend* input = nullptr;
    std::vector<std::pair<PlatformScreen*, Screen*>> screenMap;
    std::vector<Window*> windows;
    /// Window that accepted the current mouse press / touch contact: moves
    /// and the release keep going there even outside its geometry (drag).
    /// GUI thread only, cleared by removeWindow().
    Window* mouseWindow = nullptr;
    Window* touchWindow = nullptr;
    mutable std::mutex mutex; // guards screenMap and windows
};

void Platform::Impl::loadBackends(Platform* q) {
    // Backends: FIRMIR_GUI_DISPLAY / FIRMIR_GUI_INPUT pin a single backend,
    // FIRMIR_GUI_BACKENDS holds an ordered, comma-separated list of names to
    // try ("drm,eglfs") and defaults to wayland,eglfs,drm,evdev,headless.
    const char* backendsEnv = std::getenv("FIRMIR_GUI_BACKENDS");
    const std::vector<std::string> requested =
        detail::parseBackendList(backendsEnv ? backendsEnv : "");
    for (const std::string& name : requested)
        if (!detail::isDisplayBackendName(name) && !detail::isInputBackendName(name))
            cLogWarning(lcGui()) << "unknown backend in FIRMIR_GUI_BACKENDS:" << name;

    const char* displayName = std::getenv("FIRMIR_GUI_DISPLAY");
    const std::string pinnedDisplay = displayName ? displayName : "";
#ifndef _WIN32
    const bool waylandAvailable = waylandCompositorAvailable();
    const bool drmAvailable = drmDeviceExists();
#else
    // Neither wayland nor drm exists on Windows: headless is the default.
    const bool waylandAvailable = false;
    const bool drmAvailable = false;
#endif
    std::string display;
    std::vector<std::string> skipped;
    const std::vector<std::string> displayCandidates = detail::displayBackendCandidates(
        pinnedDisplay, requested, waylandAvailable, drmAvailable, &skipped);
    for (const std::string& name : skipped)
        cLogWarning(lcGui()) << "display backend skipped (unavailable):" << name;
    for (const std::string& candidate : displayCandidates) {
        displayHandle = loadPlugin("display", candidate);
        if (displayHandle) {
            display = candidate;
            break;
        }
        if (!pinnedDisplay.empty())
            throw std::runtime_error("firmir-gui: display backend not found: " +
                                     candidate);
        cLogWarning(lcGui()) << "display backend unavailable:" << candidate;
    }
    if (!displayHandle) {
        std::string message = "firmir-gui: no usable display backend";
        if (backendsEnv && *backendsEnv)
            message += std::string(" in FIRMIR_GUI_BACKENDS=") + backendsEnv;
        throw std::runtime_error(message);
    }
    auto createDisplay = reinterpret_cast<CreateDisplayFunc>(
        Firmir::detail::librarySymbol(displayHandle, FIRMIR_GUI_CREATE_DISPLAY));
    if (!createDisplay)
        throw std::runtime_error("firmir-gui: no " FIRMIR_GUI_CREATE_DISPLAY
                                 " in backend: " + display);
    this->display = createDisplay();
    this->display->screenCallback = [q](PlatformScreen* screen, bool added) {
        if (Thread::currentThread() == q->thread())
            q->d->handleScreenChanged(q, screen, added);
        else
            Object::postEvent(q,
                              std::make_unique<ScreenChangedEvent>(screen, added));
    };

    // Input: FIRMIR_GUI_INPUT, else the input-capable names of
    // FIRMIR_GUI_BACKENDS, else the display plugin's own input factory
    // (combined backends like Wayland), else evdev (pairs with DRM), else
    // headless.
    const char* inputName = std::getenv("FIRMIR_GUI_INPUT");
    const std::string pinnedInput = inputName ? inputName : "";
    const bool displayProvidesInput =
        Firmir::detail::librarySymbol(displayHandle, FIRMIR_GUI_CREATE_INPUT) != nullptr;
    std::string input;
    for (const std::string& candidate : detail::inputBackendCandidates(
             pinnedInput, requested, display, displayProvidesInput)) {
        if (candidate == display && displayProvidesInput) {
            inputHandle = displayHandle;
            input = candidate;
            break;
        }
#ifdef _WIN32
        if (candidate == "evdev")
            continue; // evdev is a Linux input backend
#endif
        inputHandle = loadPlugin("input", candidate);
        if (inputHandle) {
            input = candidate;
            break;
        }
        cLogWarning(lcGui()) << "input backend unavailable:" << candidate;
    }
    if (inputHandle) {
        auto createInput = reinterpret_cast<CreateInputFunc>(
            Firmir::detail::librarySymbol(inputHandle, FIRMIR_GUI_CREATE_INPUT));
        if (!createInput) {
            cLogWarning(lcGui()) << "no " FIRMIR_GUI_CREATE_INPUT " in backend:" << input;
            inputHandle = nullptr;
        } else {
            this->input = createInput();
            this->input->eventCallback = [q](const InputEvent& event) {
                if (Thread::currentThread() == q->thread())
                    q->d->routeInput(q, event);
                else
                    Object::postEvent(
                        q, std::make_unique<PlatformInputEvent>(event));
            };
        }
    }
}
void Platform::Impl::handleScreenChanged(Platform* q,
                                         PlatformScreen* backendScreen,
                                         bool added) {
    std::unique_lock<std::mutex> lock(mutex);
    if (added) {
        for (const auto& entry : screenMap)
            if (entry.first == backendScreen)
                return; // already known
        lock.unlock();
        Screen* screen = new Screen(backendScreen, q);
        lock.lock();
        screenMap.emplace_back(backendScreen, screen);
        lock.unlock();
        q->emitSignal(Platform::signalScreenAdded, screen);
        notifyInputScreenSize();
        return;
    }
    for (auto it = screenMap.begin(); it != screenMap.end(); ++it) {
        if (it->first != backendScreen)
            continue;
        Screen* screen = it->second;
        screenMap.erase(it);
        lock.unlock();
        screen->emitSignal(Screen::signalRemoved);
        q->emitSignal(Platform::signalScreenRemoved, screen);
        delete screen;
        notifyInputScreenSize();
        return;
    }
}

void Platform::Impl::notifyInputScreenSize() {
    if (!input)
        return;
    Size size;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!screenMap.empty())
            size = screenMap.front().first->size();
    }
    // Outside the lock: the backend logs and may call back.
    input->setScreenSize(size);
}

void Platform::Impl::routeInput(Platform*, const InputEvent& event) {
    std::vector<Window*> targets;
    {
        std::lock_guard<std::mutex> lock(mutex);
        targets = windows;
    }
    switch (event.type) {
    case InputEvent::Type::Mouse: {
        // Topmost (last registered) visible window containing the pointer.
        // While a button is held the press winner keeps the pointer, so a
        // drag that leaves the window still gets its release (and cannot
        // leave an item stuck in the pressed state).
        Window* target = nullptr;
        const bool drag =
            event.mouse.action == MouseEvent::Action::Move ||
            event.mouse.action == MouseEvent::Action::Release;
        if (drag && mouseWindow &&
            std::find(targets.begin(), targets.end(), mouseWindow) !=
                targets.end())
            target = mouseWindow;
        if (!target) {
            for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
                Window* w = *it;
                if (!w->isVisible() ||
                    !w->geometry().contains(event.mouse.pos.x,
                                            event.mouse.pos.y))
                    continue;
                target = w;
                break;
            }
        }
        if (event.mouse.action == MouseEvent::Action::Press)
            mouseWindow = target;
        else if (event.mouse.action == MouseEvent::Action::Release)
            mouseWindow = nullptr;
        if (!target)
            break;
        MouseEvent local = event.mouse;
        local.pos.x -= target->geometry().x;
        local.pos.y -= target->geometry().y;
        target->dispatchMouse(local);
        break;
    }
    case InputEvent::Type::Touch: {
        if (event.touch.points.empty())
            return;
        // Same window-level grab as for the mouse: Update/End go to the
        // window that accepted Begin even when the finger slides out of it.
        const bool drag = event.touch.action != TouchEvent::Action::Begin;
        Window* target = nullptr;
        if (drag && touchWindow &&
            std::find(targets.begin(), targets.end(), touchWindow) !=
                targets.end())
            target = touchWindow;
        if (!target) {
            const Point& pos = event.touch.points[0].pos;
            for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
                Window* w = *it;
                if (!w->isVisible() || !w->geometry().contains(pos.x, pos.y))
                    continue;
                target = w;
                break;
            }
        }
        if (event.touch.action == TouchEvent::Action::Begin)
            touchWindow = target;
        else if (event.touch.action == TouchEvent::Action::End)
            touchWindow = nullptr;
        if (!target)
            break;
        TouchEvent local = event.touch;
        for (auto& point : local.points) {
            point.pos.x -= target->geometry().x;
            point.pos.y -= target->geometry().y;
        }
        target->dispatchTouch(local);
        break;
    }
    case InputEvent::Type::Key: {
        // Deliver to the window holding the focus item, else the topmost
        // visible window.
        Window* target = nullptr;
        for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
            if (!(*it)->isVisible())
                continue;
            if ((*it)->focusItem()) {
                target = *it;
                break;
            }
            if (!target)
                target = *it;
        }
        if (target) {
            KeyEvent local = event.key;
            target->dispatchKey(local);
        }
        break;
    }
    }
}

PlatformScreen* Platform::Impl::backendFor(Screen* screen) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto& entry : screenMap)
        if (entry.second == screen)
            return entry.first;
    return nullptr;
}

Platform::Platform() : d(std::make_unique<Impl>()) {
    d->loadBackends(this);
    for (PlatformScreen* backendScreen : d->display->screens())
        d->handleScreenChanged(this, backendScreen, true);
}

Platform* Platform::instance() {
    // Intentionally leaked: backend plugins and the singleton live as long
    // as the process; the static pointer keeps it reachable for leak checkers.
    static Platform* platform = new Platform;
    return platform;
}

std::vector<Screen*> Platform::screens() const {
    std::lock_guard<std::mutex> lock(d->mutex);
    std::vector<Screen*> result;
    result.reserve(d->screenMap.size());
    for (const auto& entry : d->screenMap)
        result.push_back(entry.second);
    return result;
}

Screen* Platform::screen(const std::string& name) const {
    for (Screen* s : screens())
        if (s->name() == name)
            return s;
    return nullptr;
}

bool Platform::event(Event& event) {
    if (event.type() == ScreenChangedEventType) {
        auto& e = static_cast<ScreenChangedEvent&>(event);
        d->handleScreenChanged(this, e.screen, e.added);
        return true;
    }
    if (event.type() == InputEventType) {
        auto& e = static_cast<PlatformInputEvent&>(event);
        d->routeInput(this, e.input);
        return true;
    }
    return Object::event(event);
}

void Platform::addWindow(Window* window) {
    std::lock_guard<std::mutex> lock(d->mutex);
    d->windows.push_back(window);
}

void Platform::removeWindow(Window* window) {
    std::lock_guard<std::mutex> lock(d->mutex);
    auto& windows = d->windows;
    windows.erase(std::remove(windows.begin(), windows.end(), window),
                  windows.end());
    if (d->mouseWindow == window)
        d->mouseWindow = nullptr;
    if (d->touchWindow == window)
        d->touchWindow = nullptr;
}

PlatformWindow* Platform::createPlatformWindow(Screen* screen) {
    if (PlatformScreen* backendScreen = d->backendFor(screen))
        return d->display->createWindow(backendScreen);
    return nullptr;
}

void Platform::destroyPlatformWindow(PlatformWindow* window) {
    if (window)
        d->display->destroyWindow(window);
}

} // namespace Gui
} // namespace Firmir

