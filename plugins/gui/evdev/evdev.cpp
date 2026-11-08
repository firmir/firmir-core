// evdev input backend for firmir-gui: reads keyboard, mouse and touch
// events from /dev/input/event* devices using raw Linux evdev ioctls (no
// libinput dependency). Device hotplug is tracked with inotify on /dev/input.
//
// A single reader thread poll()s all devices and reports InputEvents through
// InputBackend::eventCallback.

#include <firmir/gui/backend.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <string>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace Firmir::Gui;

namespace {

/// Maps an evdev KEY_* code to its printable ASCII character (US layout,
/// unshifted), or 0 for non-printable keys.
char keyText(int code) {
    if (code >= KEY_1 && code <= KEY_9)
        return static_cast<char>('1' + (code - KEY_1));
    if (code == KEY_0)
        return '0';
    if (code >= KEY_A && code <= KEY_Z)
        return static_cast<char>('a' + (code - KEY_A));
    switch (code) {
    case KEY_SPACE: return ' ';
    case KEY_MINUS: return '-';
    case KEY_EQUAL: return '=';
    case KEY_COMMA: return ',';
    case KEY_DOT: return '.';
    case KEY_SLASH: return '/';
    default: return 0;
    }
}

class EvdevBackend : public InputBackend {
public:
    EvdevBackend() {
        m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (m_inotify >= 0)
            inotify_add_watch(m_inotify, "/dev/input", IN_CREATE | IN_DELETE);
        scanDevices();
        m_thread = std::thread([this] { run(); });
    }

    ~EvdevBackend() override {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
        for (const auto& device : m_devices)
            ::close(device.second);
        if (m_inotify >= 0)
            ::close(m_inotify);
    }

private:
    void scanDevices() {
        DIR* dir = ::opendir("/dev/input");
        if (!dir)
            return;
        struct dirent* ent;
        while ((ent = ::readdir(dir))) {
            if (std::strncmp(ent->d_name, "event", 5) != 0)
                continue;
            openDevice(std::string("/dev/input/") + ent->d_name);
        }
        ::closedir(dir);
    }

    void openDevice(const std::string& path) {
        for (const auto& device : m_devices)
            if (device.first == path)
                return; // already open
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            return;
        m_devices.emplace_back(path, fd);
    }

    void closeDevice(const std::string& path) {
        for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
            if (it->first == path) {
                ::close(it->second);
                m_devices.erase(it);
                return;
            }
        }
    }
    void handleInotify() {
        char buffer[4096];
        for (;;) {
            const ssize_t n = ::read(m_inotify, buffer, sizeof(buffer));
            if (n <= 0)
                break;
            for (char* p = buffer; p < buffer + n;) {
                auto* e = reinterpret_cast<struct inotify_event*>(p);
                const std::string path =
                    std::string("/dev/input/") + e->name;
                if (std::strncmp(e->name, "event", 5) == 0) {
                    if (e->mask & IN_CREATE)
                        openDevice(path);
                    if (e->mask & IN_DELETE)
                        closeDevice(path);
                }
                p += sizeof(struct inotify_event) + e->len;
            }
        }
    }

    void handleDevice(int fd) {
        struct input_event events[32];
        for (;;) {
            const ssize_t n = ::read(fd, events, sizeof(events));
            if (n <= 0)
                break;
            const int count = static_cast<int>(n / sizeof(input_event));
            for (int i = 0; i < count; ++i)
                handleEvent(events[i]);
        }
    }

    void handleEvent(const input_event& e) {
        if (!eventCallback)
            return;
        if (e.type == EV_KEY) {
            if (e.code == BTN_TOUCH) {
                m_touching = e.value != 0;
                InputEvent event;
                event.type = InputEvent::Type::Touch;
                event.touch.action = m_touching ? TouchEvent::Action::Begin
                                                : TouchEvent::Action::End;
                event.touch.points.push_back(
                    {0, {m_touchX, m_touchY}, m_touching});
                eventCallback(event);
                return;
            }
            if (e.code == BTN_LEFT || e.code == BTN_RIGHT ||
                e.code == BTN_MIDDLE) {
                InputEvent event;
                event.type = InputEvent::Type::Mouse;
                event.mouse.action = e.value ? MouseEvent::Action::Press
                                             : MouseEvent::Action::Release;
                event.mouse.button = e.code == BTN_LEFT ? 1
                                     : e.code == BTN_RIGHT ? 2 : 3;
                event.mouse.pos = {m_mouseX, m_mouseY};
                eventCallback(event);
                return;
            }
            if (e.value == 2)
                return; // ignore key repeat for now
            InputEvent event;
            event.type = InputEvent::Type::Key;
            event.key.pressed = e.value != 0;
            event.key.key = e.code;
            // Text only on press; a release carrying text would be filtered
            // by the items anyway, but keeping it empty makes that explicit.
            if (e.value != 0)
                if (const char text = keyText(e.code))
                    event.key.text = std::string(1, text);
            eventCallback(event);
            return;
        }
        if (e.type == EV_REL) {
            if (e.code == REL_WHEEL || e.code == REL_HWHEEL) {
                // evdev: positive REL_WHEEL = wheel up, REL_HWHEEL = right —
                // the same convention as MouseEvent::delta (in detents).
                InputEvent event;
                event.type = InputEvent::Type::Mouse;
                event.mouse.action = MouseEvent::Action::Wheel;
                event.mouse.pos = {m_mouseX, m_mouseY};
                event.mouse.delta =
                    e.code == REL_WHEEL
                        ? Point{0.0f, static_cast<float>(e.value)}
                        : Point{static_cast<float>(e.value), 0.0f};
                eventCallback(event);
                return;
            }
            if (e.code == REL_X)
                m_mouseX += static_cast<float>(e.value);
            else if (e.code == REL_Y)
                m_mouseY += static_cast<float>(e.value);
            else
                return;
            InputEvent event;
            event.type = InputEvent::Type::Mouse;
            event.mouse.action = MouseEvent::Action::Move;
            event.mouse.pos = {m_mouseX, m_mouseY};
            eventCallback(event);
            return;
        }
        if (e.type == EV_ABS) {
            if (e.code == ABS_X || e.code == ABS_MT_POSITION_X)
                m_touchX = static_cast<float>(e.value);
            else if (e.code == ABS_Y || e.code == ABS_MT_POSITION_Y)
                m_touchY = static_cast<float>(e.value);
            else
                return;
            if (m_touching) {
                InputEvent event;
                event.type = InputEvent::Type::Touch;
                event.touch.action = TouchEvent::Action::Update;
                event.touch.points.push_back({0, {m_touchX, m_touchY}, true});
                eventCallback(event);
            }
            return;
        }
    }

    void run() {
        while (!m_stop) {
            std::vector<pollfd> pollfds;
            if (m_inotify >= 0)
                pollfds.push_back({m_inotify, POLLIN, 0});
            for (const auto& device : m_devices)
                pollfds.push_back({device.second, POLLIN, 0});
            if (pollfds.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            const int ready = ::poll(pollfds.data(), pollfds.size(), 100);
            if (ready <= 0)
                continue;
            std::size_t index = 0;
            if (m_inotify >= 0) {
                if (pollfds[0].revents & POLLIN)
                    handleInotify();
                index = 1;
            }
            // Snapshot the fd list: handleInotify() may have changed it.
            std::vector<int> fds;
            for (const auto& device : m_devices)
                fds.push_back(device.second);
            for (int fd : fds) {
                for (std::size_t i = index; i < pollfds.size(); ++i) {
                    if (pollfds[i].fd == fd && (pollfds[i].revents & POLLIN))
                        handleDevice(fd);
                }
            }
        }
    }

    std::atomic<bool> m_stop{false};
    std::thread m_thread;
    int m_inotify = -1;
    std::vector<std::pair<std::string, int>> m_devices;
    float m_mouseX = 0;
    float m_mouseY = 0;
    float m_touchX = 0;
    float m_touchY = 0;
    bool m_touching = false;
};

} // namespace

extern "C" InputBackend* firmir_gui_create_input() {
    return new EvdevBackend;
}

