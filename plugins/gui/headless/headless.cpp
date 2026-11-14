// Headless firmir-gui backend: virtual screens and windows kept in memory,
// synthetic input injection. Used for tests and as the default fallback
// backend when no real display backend is selected.

#include <firmir/gui/backend.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace Firmir::Gui;

namespace {

class HeadlessScreen : public PlatformScreen {
public:
    HeadlessScreen(std::string name, Size size)
        : m_name(std::move(name)), m_size(size) {}
    std::string name() const override { return m_name; }
    Size size() const override { return m_size; }

private:
    std::string m_name;
    Size m_size;
};

class HeadlessWindow : public PlatformWindow {
public:
    void setTitle(const std::string& title) override { m_title = title; }
    void setGeometry(const Rect& geometry) override { m_geometry = geometry; }
    void setVisible(bool visible) override { m_visible = visible; }
    void present(const std::uint32_t* pixels, const Size& size) override {
        m_presented.fetch_add(1, std::memory_order_relaxed);
        // Test hook: a slow present emulates a backend that waits for the
        // display refresh (KMS page flip), which is how the GUI thread used to
        // be blocked for a whole frame.
        const int delay = m_presentDelayMs.load(std::memory_order_relaxed);
        if (delay > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_frame.assign(pixels,
                       pixels +
                           static_cast<std::size_t>(size.width) * size.height);
        m_frameSize = size;
        m_frameCount.fetch_add(1, std::memory_order_relaxed);
    }

    // Test access
    std::string m_title;
    Rect m_geometry;
    bool m_visible = false;
    std::mutex m_frameMutex;
    std::vector<std::uint32_t> m_frame;
    Size m_frameSize;
    std::atomic<int> m_frameCount{0};
    /// Frames whose present() was entered (incremented before the delay).
    std::atomic<int> m_presented{0};
    std::atomic<int> m_presentDelayMs{0};
};

class HeadlessBackend : public DisplayBackend, public InputBackend {
public:
    static HeadlessBackend& instance() {
        static HeadlessBackend backend;
        return backend;
    }

    // DisplayBackend
    std::vector<PlatformScreen*> screens() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<PlatformScreen*> result;
        for (const auto& screen : m_screens)
            result.push_back(screen.get());
        return result;
    }

    PlatformWindow* createWindow(PlatformScreen*) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_windows.push_back(std::make_unique<HeadlessWindow>());
        return m_windows.back().get();
    }

    void destroyWindow(PlatformWindow* window) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_windows.begin(); it != m_windows.end(); ++it) {
            if (it->get() == window) {
                m_windows.erase(it);
                return;
            }
        }
    }

    // Test control
    void addScreen(const std::string& name, int width, int height) {
        PlatformScreen* screen;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_screens.push_back(
                std::make_unique<HeadlessScreen>(name, Size{width, height}));
            screen = m_screens.back().get();
        }
        if (screenCallback)
            screenCallback(screen, true);
    }

    void removeScreen(const std::string& name) {
        PlatformScreen* screen = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_screens.begin(); it != m_screens.end(); ++it) {
                if ((*it)->name() == name) {
                    screen = it->get();
                    m_screens.erase(it);
                    break;
                }
            }
        }
        if (screen && screenCallback)
            screenCallback(screen, false);
    }

    void inject(const InputEvent& event) {
        if (eventCallback)
            eventCallback(event);
    }

    HeadlessWindow* windowAt(int index) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (index < 0 || index >= static_cast<int>(m_windows.size()))
            return nullptr;
        return m_windows[static_cast<std::size_t>(index)].get();
    }

    int windowCount() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return static_cast<int>(m_windows.size());
    }

private:
    HeadlessBackend() = default;

    std::mutex m_mutex;
    std::vector<std::unique_ptr<HeadlessScreen>> m_screens;
    std::vector<std::unique_ptr<HeadlessWindow>> m_windows;
};

} // namespace
extern "C" {

DisplayBackend* firmir_gui_create_display() {
    return &HeadlessBackend::instance();
}

InputBackend* firmir_gui_create_input() {
    return &HeadlessBackend::instance();
}

void firmir_gui_headless_add_screen(const char* name, int width, int height) {
    HeadlessBackend::instance().addScreen(name, width, height);
}

void firmir_gui_headless_remove_screen(const char* name) {
    HeadlessBackend::instance().removeScreen(name);
}

void firmir_gui_headless_inject_mouse(int action, float x, float y,
                                      int button) {
    InputEvent event;
    event.type = InputEvent::Type::Mouse;
    event.mouse.action = static_cast<MouseEvent::Action>(action);
    event.mouse.pos = {x, y};
    event.mouse.button = button;
    HeadlessBackend::instance().inject(event);
}

void firmir_gui_headless_inject_wheel(float x, float y, float dx, float dy) {
    InputEvent event;
    event.type = InputEvent::Type::Mouse;
    event.mouse.action = MouseEvent::Action::Wheel;
    event.mouse.pos = {x, y};
    event.mouse.delta = {dx, dy};
    HeadlessBackend::instance().inject(event);
}

void firmir_gui_headless_inject_key(bool pressed, int key, const char* text) {
    InputEvent event;
    event.type = InputEvent::Type::Key;
    event.key.pressed = pressed;
    event.key.key = key;
    event.key.text = text ? text : "";
    HeadlessBackend::instance().inject(event);
}

void firmir_gui_headless_inject_touch(int action, int id, float x, float y) {
    InputEvent event;
    event.type = InputEvent::Type::Touch;
    event.touch.action = static_cast<TouchEvent::Action>(action);
    event.touch.points.push_back(
        {id, {x, y}, action != static_cast<int>(TouchEvent::Action::End)});
    HeadlessBackend::instance().inject(event);
}

int firmir_gui_headless_window_count() {
    return HeadlessBackend::instance().windowCount();
}

int firmir_gui_headless_present_count(int index) {
    HeadlessWindow* window = HeadlessBackend::instance().windowAt(index);
    return window ? window->m_presented.load(std::memory_order_relaxed) : 0;
}

void firmir_gui_headless_set_present_delay_ms(int index, int milliseconds) {
    HeadlessWindow* window = HeadlessBackend::instance().windowAt(index);
    if (window)
        window->m_presentDelayMs.store(milliseconds,
                                       std::memory_order_relaxed);
}

bool firmir_gui_headless_last_frame(int index, std::uint32_t* outPixels,
                                    int* outWidth, int* outHeight) {
    HeadlessWindow* window = HeadlessBackend::instance().windowAt(index);
    if (!window)
        return false;
    std::lock_guard<std::mutex> lock(window->m_frameMutex);
    if (window->m_frame.empty())
        return false;
    if (outPixels && outWidth && *outWidth == window->m_frameSize.width &&
        outHeight && *outHeight == window->m_frameSize.height) {
        std::memcpy(outPixels, window->m_frame.data(),
                    window->m_frame.size() * sizeof(std::uint32_t));
    }
    if (outWidth)
        *outWidth = window->m_frameSize.width;
    if (outHeight)
        *outHeight = window->m_frameSize.height;
    return true;
}

int firmir_gui_headless_frame_count(int index) {
    HeadlessWindow* window = HeadlessBackend::instance().windowAt(index);
    return window ? window->m_frameCount.load(std::memory_order_relaxed) : 0;
}
}

