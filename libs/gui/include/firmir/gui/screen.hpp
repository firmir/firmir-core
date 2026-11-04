#pragma once

#include <firmir/gui/events.hpp>
#include <firmir/object.hpp>

#include <memory>
#include <string>

namespace Firmir {
namespace Gui {

class PlatformScreen;
class Platform;

/**
 * @brief A screen that windows can be placed on.
 *
 * Screens come and go at runtime (display hotplug): the Platform singleton
 * emits Platform::signalScreenAdded / Platform::signalScreenRemoved, and a
 * screen that is about to disappear emits Screen::signalRemoved before it
 * is destroyed.
 */
class Screen : public Object {
public:
    ~Screen() override;

    /// Backend-provided screen name (e.g. "HDMI-A-1").
    std::string name() const;
    /// Pixel size of the screen.
    Size size() const;

    /// Signal "removed" — emitted just before the screen is destroyed.
    static constexpr auto signalRemoved = "removed";

private:
    explicit Screen(PlatformScreen* backend, Object* parent);

    class Impl;
    std::unique_ptr<Impl> d;

    friend class Platform;
};

} // namespace Gui
} // namespace Firmir
