#include <firmir/gui/platform.hpp>

#include <algorithm>
#include <cstdlib>
#include <dlfcn.h>
#include <firmir/thread.hpp>
#include <firmir/gui/backend.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/log.hpp>
#include <mutex>
#include <stdexcept>
#include <unistd.h>

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

/// Directories searched for firmir-gui-<name>.so plugins:
/// FIRMIR_GUI_PLUGIN_PATH (colon-separated), then directories relative to
/// the running executable (build tree and installed layouts).
std::vector<std::string> pluginDirs() {
    std::vector<std::string> dirs;
    const char* path = std::getenv("FIRMIR_GUI_PLUGIN_PATH");
    if (path && *path) {
        std::string rest = path;
        for (;;) {
            const std::size_t colon = rest.find(':');
            dirs.push_back(rest.substr(0, colon));
            if (colon == std::string::npos)
                break;
            rest.erase(0, colon + 1);
        }
    }
    char exePath[4096];
    const ssize_t len =
        ::readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len > 0) {
        exePath[len] = '\0';
        std::string dir = exePath;
        const std::size_t slash = dir.rfind('/');
        if (slash != std::string::npos) {
            dir.erase(slash);
            dirs.push_back(dir + "/../plugins");
            dirs.push_back(dir + "/../../plugins");
            dirs.push_back(dir + "/../../../plugins");
            dirs.push_back(dir + "/plugins");
        }
    }
#ifdef FIRMIR_SYSTEM_PLUGIN_DIR
    dirs.push_back(FIRMIR_SYSTEM_PLUGIN_DIR);
#endif
    return dirs;
}

/// True when a Wayland compositor socket exists ($XDG_RUNTIME_DIR/
/// $WAYLAND_DISPLAY, defaulting to wayland-0).
bool waylandSocketExists() {
    const char* runtimeDir = std::getenv("XDG_RUNTIME_DIR");
    if (!runtimeDir || !*runtimeDir)
        return false;
    const char* display = std::getenv("WAYLAND_DISPLAY");
    const std::string socket = std::string(runtimeDir) + "/" +
        (display && *display ? display : "wayland-0");
    return ::access(socket.c_str(), F_OK) == 0;
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

void* loadPlugin(const char* kind, const std::string& name) {
    const std::string file = "firmir-gui-" + name + ".so";
    for (const std::string& dir : pluginDirs()) {
        const std::string path = dir + "/" + file;
        if (void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL)) {
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

    void* displayHandle = nullptr;
    void* inputHandle = nullptr;
    DisplayBackend* display = nullptr;
    InputBackend* input = nullptr;
    std::vector<std::pair<PlatformScreen*, Screen*>> screenMap;
    std::vector<Window*> windows;
    mutable std::mutex mutex; // guards screenMap and windows
};

void Platform::Impl::loadBackends(Platform* q) {
    // Display backend: FIRMIR_GUI_DISPLAY when set, otherwise the first
    // loadable of: wayland (when a compositor socket exists), drm (with
    // evdev input, when a /dev/dri/cardN node exists), headless.
    const char* displayName = std::getenv("FIRMIR_GUI_DISPLAY");
    std::vector<std::string> candidates;
    if (displayName && *displayName) {
        candidates.push_back(displayName);
    } else {
        if (waylandSocketExists())
            candidates.push_back("wayland");
        if (drmDeviceExists())
            candidates.push_back("drm");
        candidates.push_back("headless");
    }
    std::string display;
    for (const std::string& candidate : candidates) {
        displayHandle = loadPlugin("display", candidate);
        if (displayHandle) {
            display = candidate;
            break;
        }
        if (displayName && *displayName)
            throw std::runtime_error("firmir-gui: display backend not found: " +
                                     candidate);
        cLogWarning(lcGui()) << "display backend unavailable:" << candidate;
    }
    if (!displayHandle)
        throw std::runtime_error("firmir-gui: no usable display backend");
    auto createDisplay = reinterpret_cast<CreateDisplayFunc>(
        dlsym(displayHandle, FIRMIR_GUI_CREATE_DISPLAY));
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

    // Input: an explicitly named plugin, else the display plugin's own input
    // factory (combined backends like Wayland), else evdev (pairs with DRM),
    // else headless.
    const char* inputName = std::getenv("FIRMIR_GUI_INPUT");
    if (inputName && *inputName) {
        inputHandle = loadPlugin("input", inputName);
    } else if (dlsym(displayHandle, FIRMIR_GUI_CREATE_INPUT)) {
        inputHandle = displayHandle;
    } else if (display != "headless") {
        inputHandle = loadPlugin("input", "evdev");
        if (!inputHandle)
            inputHandle = loadPlugin("input", "headless");
    }
    if (inputHandle) {
        auto createInput = reinterpret_cast<CreateInputFunc>(
            dlsym(inputHandle, FIRMIR_GUI_CREATE_INPUT));
        if (createInput) {
            input = createInput();
            input->eventCallback = [q](const InputEvent& event) {
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
        return;
    }
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
        for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
            Window* w = *it;
            if (!w->isVisible() || !w->geometry().contains(event.mouse.pos.x,
                                                           event.mouse.pos.y))
                continue;
            MouseEvent local = event.mouse;
            local.pos.x -= w->geometry().x;
            local.pos.y -= w->geometry().y;
            w->dispatchMouse(local);
            return;
        }
        break;
    }
    case InputEvent::Type::Touch: {
        if (event.touch.points.empty())
            return;
        const Point& pos = event.touch.points[0].pos;
        for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
            Window* w = *it;
            if (!w->isVisible() || !w->geometry().contains(pos.x, pos.y))
                continue;
            TouchEvent local = event.touch;
            for (auto& point : local.points) {
                point.pos.x -= w->geometry().x;
                point.pos.y -= w->geometry().y;
            }
            w->dispatchTouch(local);
            return;
        }
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

