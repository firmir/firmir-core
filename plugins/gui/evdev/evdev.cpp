// evdev input backend for firmir-gui: reads keyboard, mouse and touch
// events from /dev/input/event* devices using raw Linux evdev ioctls (no
// libinput dependency). Device hotplug is tracked with inotify on /dev/input.
//
// A single reader thread poll()s all devices and reports InputEvents through
// InputBackend::eventCallback.
//
// Absolute (touchscreen) reports are device coordinates, not pixels: each device
// is queried for its axis ranges (EVIOCGABS) and mapped onto the primary screen
// handed over by Platform (InputBackend::setScreenSize) — see touchmapping.hpp.
//
// Environment:
//   FIRMIR_EVDEV_DEBUG=1        list every /dev/input/event* device at startup
//                               and on hotplug: name, phys, kind and axes
//   FIRMIR_EVDEV_DEBUG=events   ... and every event that is read
//   FIRMIR_EVDEV_TOUCH_ROTATION=auto|0|90|180|270  digitizer → display rotation
//                               (default auto: swap the axes when the digitizer's
//                               aspect differs from the screen's)
//   FIRMIR_EVDEV_TOUCH_FLIP=none|x|y|both          mirror the mapped position
//   FIRMIR_EVDEV_TOUCH_DEVICE=/dev/input/eventN    only this node is a digitizer

#include <firmir/gui/backend.hpp>
#include <firmir/log.hpp>

#include "touchmapping.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <string>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace Firmir::Gui;
namespace touch = Firmir::Evdev::detail;

namespace {

const Firmir::LoggingCategory& lcEvdev() {
    static const Firmir::LoggingCategory category("gui.evdev");
    return category;
}

/// How much of the device activity FIRMIR_EVDEV_DEBUG asks for.
enum class Debug { Off, Devices, Events };

Debug debugLevel() {
    const char* value = std::getenv("FIRMIR_EVDEV_DEBUG");
    if (!value || !*value)
        return Debug::Off;
    if (std::strcmp(value, "events") == 0 || std::strcmp(value, "2") == 0)
        return Debug::Events;
    return Debug::Devices; // "1", "all", anything else non-empty
}

bool debugDevices() {
    static const bool enabled = debugLevel() != Debug::Off;
    return enabled;
}

bool debugEvents() {
    static const bool enabled = debugLevel() == Debug::Events;
    return enabled;
}

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

/// One bit of an EVIOCGBIT / EVIOCGPROP reply.
bool bitSet(const unsigned long* bits, int bit) {
    const std::size_t perWord = 8 * sizeof(unsigned long);
    const std::size_t word = static_cast<std::size_t>(bit) / perWord;
    return ((bits[word] >> (static_cast<std::size_t>(bit) % perWord)) & 1ul) != 0;
}

constexpr std::size_t bitWords(int max) {
    return static_cast<std::size_t>(max) / (8 * sizeof(unsigned long)) + 1;
}

/// What one /dev/input/event* device can report (EVIOCG* ioctls).
struct Capabilities {
    std::string name;
    std::string phys;
    bool key = false;
    bool rel = false;
    bool abs = false;
    /// INPUT_PROP_DIRECT: a digitizer mounted on the display.
    bool direct = false;
    bool touchButton = false;   ///< BTN_TOUCH
    bool multiTouch = false;    ///< ABS_MT_POSITION_X/Y
    bool mouseButtons = false;  ///< BTN_LEFT
    bool wheel = false;         ///< REL_WHEEL / REL_HWHEEL
    bool letterKeys = false;    ///< KEY_A..KEY_Z
    bool relativePointer = false; ///< REL_X + REL_Y
    bool absolutePointer = false; ///< ABS_X + ABS_Y (or the MT pair)
    int xCode = ABS_X;
    int yCode = ABS_Y;
    touch::TouchAxis xAxis;
    touch::TouchAxis yAxis;
    int xResolution = 0;
    int yResolution = 0;
};

/// Device classes this backend acts on.
enum class DeviceKind { Touch, Mouse, Keyboard, Other };

const char* kindName(DeviceKind kind) {
    switch (kind) {
    case DeviceKind::Touch: return "touchscreen";
    case DeviceKind::Mouse: return "mouse";
    case DeviceKind::Keyboard: return "keyboard";
    case DeviceKind::Other: break;
    }
    return "other";
}

DeviceKind classify(const Capabilities& caps) {
    if (caps.abs && caps.absolutePointer &&
        (caps.direct || caps.touchButton || caps.multiTouch))
        return DeviceKind::Touch;
    if (caps.rel && caps.relativePointer)
        return DeviceKind::Mouse;
    if (caps.key && (caps.letterKeys || !caps.mouseButtons))
        return DeviceKind::Keyboard;
    return DeviceKind::Other;
}

Capabilities queryCapabilities(int fd) {
    Capabilities caps;
    char text[256] = {};
    if (::ioctl(fd, EVIOCGNAME(sizeof(text) - 1), text) >= 0)
        caps.name = text;
    std::memset(text, 0, sizeof(text));
    if (::ioctl(fd, EVIOCGPHYS(sizeof(text) - 1), text) >= 0)
        caps.phys = text;

    unsigned long events[bitWords(EV_MAX)] = {};
    if (::ioctl(fd, EVIOCGBIT(0, sizeof(events)), events) < 0)
        return caps; // not an evdev device we can read
    caps.key = bitSet(events, EV_KEY);
    caps.rel = bitSet(events, EV_REL);
    caps.abs = bitSet(events, EV_ABS);

    if (caps.abs) {
        unsigned long axes[bitWords(ABS_MAX)] = {};
        if (::ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(axes)), axes) >= 0) {
            caps.multiTouch = bitSet(axes, ABS_MT_POSITION_X) &&
                bitSet(axes, ABS_MT_POSITION_Y);
            caps.absolutePointer =
                (bitSet(axes, ABS_X) && bitSet(axes, ABS_Y)) || caps.multiTouch;
            // Multi-touch devices report per-slot positions; prefer those.
            caps.xCode = caps.multiTouch ? ABS_MT_POSITION_X : ABS_X;
            caps.yCode = caps.multiTouch ? ABS_MT_POSITION_Y : ABS_Y;
        }
        if (caps.absolutePointer) {
            input_absinfo info{};
            if (::ioctl(fd, EVIOCGABS(caps.xCode), &info) >= 0) {
                caps.xAxis = {info.minimum, info.maximum};
                caps.xResolution = info.resolution;
            }
            std::memset(&info, 0, sizeof(info));
            if (::ioctl(fd, EVIOCGABS(caps.yCode), &info) >= 0) {
                caps.yAxis = {info.minimum, info.maximum};
                caps.yResolution = info.resolution;
            }
        }
    }
    if (caps.key) {
        unsigned long keys[bitWords(KEY_MAX)] = {};
        if (::ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0) {
            caps.touchButton = bitSet(keys, BTN_TOUCH);
            caps.mouseButtons = bitSet(keys, BTN_LEFT);
            for (int code = KEY_A; code <= KEY_Z && !caps.letterKeys; ++code)
                caps.letterKeys = bitSet(keys, code);
        }
    }
    if (caps.rel) {
        unsigned long rels[bitWords(REL_MAX)] = {};
        if (::ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rels)), rels) >= 0) {
            caps.relativePointer = bitSet(rels, REL_X) && bitSet(rels, REL_Y);
            caps.wheel = bitSet(rels, REL_WHEEL) || bitSet(rels, REL_HWHEEL);
        }
    }
    unsigned long props[bitWords(INPUT_PROP_MAX)] = {};
    if (::ioctl(fd, EVIOCGPROP(sizeof(props)), props) >= 0)
        caps.direct = bitSet(props, INPUT_PROP_DIRECT);
    return caps;
}

/// Touch configuration from the environment (parsed once).
struct TouchConfig {
    /// Resolve the rotation from the digitizer/screen aspect ratios.
    bool autoRotation = true;
    touch::TouchTransform transform;
    /// Only this node acts as a digitizer (empty = every digitizer found).
    std::string device;
};

TouchConfig parseTouchConfig() {
    TouchConfig config;
    if (const char* rotation = std::getenv("FIRMIR_EVDEV_TOUCH_ROTATION")) {
        const std::string value = rotation;
        if (value == "90")
            config.transform.rotation = touch::TouchRotation::Clockwise90;
        else if (value == "180")
            config.transform.rotation = touch::TouchRotation::Clockwise180;
        else if (value == "270")
            config.transform.rotation = touch::TouchRotation::Clockwise270;
        else if (value == "0")
            config.transform.rotation = touch::TouchRotation::None;
        else if (!value.empty() && value != "auto")
            cLogWarning(lcEvdev()) << "unknown FIRMIR_EVDEV_TOUCH_ROTATION"
                                   << value << "- expected auto, 0, 90, 180, 270";
        config.autoRotation = value.empty() || value == "auto";
    }
    if (const char* flip = std::getenv("FIRMIR_EVDEV_TOUCH_FLIP")) {
        const std::string value = flip;
        if (value == "x" || value == "both")
            config.transform.mirrorX = true;
        if (value == "y" || value == "both")
            config.transform.mirrorY = true;
        if (!value.empty() && value != "none" && value != "x" && value != "y" &&
            value != "both")
            cLogWarning(lcEvdev()) << "unknown FIRMIR_EVDEV_TOUCH_FLIP" << value
                                   << "- expected none, x, y or both";
    }
    if (const char* device = std::getenv("FIRMIR_EVDEV_TOUCH_DEVICE"))
        config.device = device;
    return config;
}

const TouchConfig& touchConfig() {
    static const TouchConfig config = parseTouchConfig();
    return config;
}

std::string transformText(const touch::TouchTransform& transform) {
    std::string text =
        "rotation " + std::to_string(static_cast<int>(transform.rotation));
    if (transform.mirrorX)
        text += " +flip-x";
    if (transform.mirrorY)
        text += " +flip-y";
    return text;
}

/// One open /dev/input/event* device.
struct Device {
    std::string path;
    int fd = -1;
    Capabilities caps;
    DeviceKind kind = DeviceKind::Other;
    /// The node is the digitizer whose ABS reports become touch positions.
    bool digitizer = false;
    /// Last raw report, in device units.
    float rawX = 0;
    float rawY = 0;
    bool hasPosition = false;
    bool touching = false;
    /// Mapping already logged for this screen generation.
    unsigned mappingGeneration = 0;
};


class EvdevBackend : public InputBackend {
public:
    EvdevBackend() {
        touchConfig(); // parse FIRMIR_EVDEV_TOUCH_* early, so warnings show up
        m_inotify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
        if (m_inotify >= 0)
            inotify_add_watch(m_inotify, "/dev/input", IN_CREATE | IN_DELETE);
        scanDevices();
        if (debugDevices())
            logStartupSummary();
        else
            cLogInfo(lcEvdev())
                << "opened" << m_devices.size()
                << "device(s) in /dev/input (FIRMIR_EVDEV_DEBUG=1 lists them)";
        m_thread = std::thread([this] { run(); });
    }

    ~EvdevBackend() override {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
        for (const Device& device : m_devices)
            ::close(device.fd);
        if (m_inotify >= 0)
            ::close(m_inotify);
    }

    /// Called by Platform with the primary screen size: touch positions are
    /// scaled to it (see touchmapping.hpp).
    void setScreenSize(const Size& size) override {
        const int previousWidth = m_screenWidth.exchange(size.width);
        const int previousHeight = m_screenHeight.exchange(size.height);
        if (previousWidth == size.width && previousHeight == size.height)
            return;
        ++m_screenGeneration;
        if (debugDevices())
            cLogInfo(lcEvdev()) << "screen size" << size.width << size.height
                                << "- touch positions are scaled to it";
    }

private:
    void scanDevices() {
        DIR* dir = ::opendir("/dev/input");
        if (!dir) {
            cLogWarning(lcEvdev()) << "cannot read /dev/input:"
                                   << std::strerror(errno);
            return;
        }
        struct dirent* ent;
        while ((ent = ::readdir(dir))) {
            if (std::strncmp(ent->d_name, "event", 5) != 0)
                continue;
            openDevice(std::string("/dev/input/") + ent->d_name);
        }
        ::closedir(dir);
    }

    void openDevice(const std::string& path) {
        for (const Device& device : m_devices)
            if (device.path == path)
                return; // already open
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            if (debugDevices())
                cLogInfo(lcEvdev()) << "cannot open" << path << ":"
                                    << std::strerror(errno);
            return;
        }
        Device device;
        device.path = path;
        device.fd = fd;
        device.caps = queryCapabilities(fd);
        device.kind = classify(device.caps);
        const TouchConfig& config = touchConfig();
        device.digitizer = device.kind == DeviceKind::Touch &&
            (config.device.empty() || config.device == path);
        if (device.kind == DeviceKind::Touch && !device.digitizer) {
            // Another node was pinned as the digitizer.
            device.kind = DeviceKind::Other;
        }
        m_devices.push_back(device);
        if (debugDevices())
            describeDevice(m_devices.back());
    }

    /// Summary printed after the initial scan: the per-device lines come from
    /// openDevice(). This is what FIRMIR_EVDEV_DEBUG=1 shows at startup.
    void logStartupSummary() {
        cLogInfo(lcEvdev()) << "scanning /dev/input:" << m_devices.size()
                            << "device(s)";
        const Size screen{m_screenWidth.load(), m_screenHeight.load()};
        cLogInfo(lcEvdev()) << "screen size" << screen.width << screen.height
                            << "- touch positions are scaled to it";
    }

    /// One line per device: what it is, and the axis ranges taps are mapped from.
    void describeDevice(const Device& device) {
        const Capabilities& caps = device.caps;
        Firmir::Log log = cLogInfo(lcEvdev());
        log << device.path << "\"" << (caps.name.empty() ? "?" : caps.name)
            << "\"";
        if (!caps.phys.empty())
            log << "phys" << caps.phys;
        log << "type" << kindName(device.kind);
        std::string types;
        if (caps.key)
            types = "EV_KEY";
        if (caps.abs)
            types += types.empty() ? "EV_ABS" : ",EV_ABS";
        if (caps.rel)
            types += types.empty() ? "EV_REL" : ",EV_REL";
        log << (types.empty() ? "no-input-events" : types);
        if (caps.absolutePointer)
            log << "abs-x" << caps.xCode << "[" << caps.xAxis.minimum << ".."
                << caps.xAxis.maximum << "]"
                << "abs-y" << caps.yCode << "[" << caps.yAxis.minimum << ".."
                << caps.yAxis.maximum << "]";
        if (caps.xResolution > 0 || caps.yResolution > 0)
            log << "resolution" << caps.xResolution << caps.yResolution;
        if (caps.direct)
            log << "direct";
        if (caps.multiTouch)
            log << "multi-touch";
        if (caps.touchButton)
            log << "btn-touch";
        if (caps.relativePointer)
            log << "rel-pointer";
    }


    void closeDevice(const std::string& path) {
        for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
            if (it->path != path)
                continue;
            if (debugDevices())
                cLogInfo(lcEvdev()) << "removed" << path << "\""
                                    << it->caps.name << "\"";
            ::close(it->fd);
            m_devices.erase(it);
            return;
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

    void handleDevice(Device& device) {
        struct input_event events[32];
        for (;;) {
            const ssize_t n = ::read(device.fd, events, sizeof(events));
            if (n <= 0)
                break;
            const int count = static_cast<int>(n / sizeof(input_event));
            for (int i = 0; i < count; ++i)
                handleEvent(device, events[i]);
        }
    }

    /// The digitizer's raw axes mapped onto the screen, plus a one-shot log line
    /// saying how they were mapped (rotation, ranges, screen).
    Point touchPosition(Device& device) {
        touch::TouchArea area;
        area.x = device.caps.xAxis;
        area.y = device.caps.yAxis;
        area.screenWidth = m_screenWidth.load();
        area.screenHeight = m_screenHeight.load();

        touch::TouchTransform transform = touchConfig().transform;
        if (touchConfig().autoRotation)
            transform.rotation = touch::touchAutoRotation(area);

        const unsigned generation = m_screenGeneration.load();
        if (device.mappingGeneration != generation && debugDevices()) {
            device.mappingGeneration = generation;
            // A device whose axes have no range (0..0) falls back to its own
            // extent, so the log always shows the pixel grid taps land on.
            const int width = area.screenWidth > 0
                                  ? area.screenWidth
                                  : area.x.maximum - area.x.minimum + 1;
            const int height = area.screenHeight > 0
                                   ? area.screenHeight
                                   : area.y.maximum - area.y.minimum + 1;
            cLogInfo(lcEvdev())
                << device.path << "\"" << device.caps.name << "\""
                << "touch x[" << area.x.minimum << ".." << area.x.maximum
                << "] y[" << area.y.minimum << ".." << area.y.maximum << "]"
                << "mapped to" << width << height << transformText(transform)
                << (touchConfig().autoRotation ? "(auto)" : "")
                << "- if taps land elsewhere, set"
                << "FIRMIR_EVDEV_TOUCH_ROTATION=0|90|180|270"
                << "or FIRMIR_EVDEV_TOUCH_FLIP=x|y|both";
        }
        const touch::TouchPixel pixel =
            touch::mapTouchPosition(area, transform, device.rawX, device.rawY);
        return {pixel.x, pixel.y};
    }

    void logEvent(const Device& device, const input_event& e) {
        Firmir::Log log = cLogInfo(lcEvdev());
        log << device.path << "type" << e.type << "code" << e.code << "value"
            << e.value;
        if (device.digitizer && (e.code == device.caps.xCode ||
                                 e.code == device.caps.yCode))
            log << "raw" << device.rawX << device.rawY;
    }


    void emitTouch(Device& device, TouchEvent::Action action) {
        InputEvent event;
        event.type = InputEvent::Type::Touch;
        event.touch.action = action;
        const Point pos = touchPosition(device);
        event.touch.points.push_back(
            {0, pos, action != TouchEvent::Action::End});
        if (debugEvents())
            cLogInfo(lcEvdev()) << "touch" << device.path << "action"
                                << static_cast<int>(action) << "raw"
                                << device.rawX << device.rawY << "pos" << pos.x
                                << pos.y;
        eventCallback(event);
    }

    void handleEvent(Device& device, const input_event& e) {
        if (!eventCallback)
            return;
        if (debugEvents())
            logEvent(device, e);
        if (e.type == EV_KEY) {
            if (e.code == BTN_TOUCH && device.digitizer) {
                device.touching = e.value != 0;
                emitTouch(device, device.touching ? TouchEvent::Action::Begin
                                                  : TouchEvent::Action::End);
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
            if (!device.digitizer)
                return; // absolute axes of a non-digitizer carry no touch
            if (e.code == device.caps.xCode) {
                device.rawX = static_cast<float>(e.value);
                device.hasPosition = true;
            } else if (e.code == device.caps.yCode) {
                device.rawY = static_cast<float>(e.value);
                device.hasPosition = true;
            } else if (e.code == ABS_MT_TRACKING_ID &&
                       !device.caps.touchButton) {
                // Multi-touch protocol B without BTN_TOUCH: the tracking id
                // says when the contact starts and ends.
                device.touching = e.value >= 0;
                if (device.hasPosition)
                    emitTouch(device, device.touching
                                          ? TouchEvent::Action::Begin
                                          : TouchEvent::Action::End);
                return;
            } else {
                return;
            }
            if (device.touching && device.hasPosition)
                emitTouch(device, TouchEvent::Action::Update);
            return;
        }
    }


    void run() {
        while (!m_stop) {
            std::vector<pollfd> pollfds;
            if (m_inotify >= 0)
                pollfds.push_back({m_inotify, POLLIN, 0});
            for (const Device& device : m_devices)
                pollfds.push_back({device.fd, POLLIN, 0});
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
            for (const Device& device : m_devices)
                fds.push_back(device.fd);
            for (const int fd : fds) {
                // Look the device up by fd: the vector may have been reallocated.
                Device* device = nullptr;
                for (Device& candidate : m_devices)
                    if (candidate.fd == fd)
                        device = &candidate;
                if (!device)
                    continue;
                for (std::size_t i = index; i < pollfds.size(); ++i) {
                    if (pollfds[i].fd == fd && (pollfds[i].revents & POLLIN))
                        handleDevice(*device);
                }
            }
        }
    }

    std::atomic<bool> m_stop{false};
    std::thread m_thread;
    int m_inotify = -1;
    std::vector<Device> m_devices;
    /// Primary screen size, pushed by Platform (0 = unknown).
    std::atomic<int> m_screenWidth{0};
    std::atomic<int> m_screenHeight{0};
    /// Bumped whenever the screen size changes, so the mapping log is repeated.
    std::atomic<unsigned> m_screenGeneration{0};
    float m_mouseX = 0;
    float m_mouseY = 0;
};

} // namespace

extern "C" InputBackend* firmir_gui_create_input() {
    return new EvdevBackend;
}

