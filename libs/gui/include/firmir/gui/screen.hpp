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
    /// Physical size of the panel in millimetres, {0, 0} when the backend does
    /// not report it.
    Size physicalSize() const;

    /**
     * @brief Pixel density of the screen.
     *
     * Derived from size() and physicalSize() along the diagonal, so the panel
     * bezel and the aspect ratio do not skew the value. Both return 0 when the
     * backend does not know the physical size.
     *
     * Layout code sizes touch targets from them (millimetres are what a finger
     * can hit): a 36 px button is ~3 mm on a dense tablet panel and ~9 mm on a
     * desktop display. Both are exposed as the read-only Number properties
     * "ppi" and "ppcm" here and on the window that shows the screen
     * (Window::ppi/ppcm, which Firml bindings watch and which re-emits them
     * when the window moves to another screen).
     */
    double ppi() const;
    /// Pixels per centimetre, same source as ppi().
    double ppcm() const;

    /// Signal "removed" — emitted just before the screen is destroyed.
    static constexpr auto signalRemoved = "removed";

private:
    explicit Screen(PlatformScreen* backend, Object* parent);

    /// The density properties are read-only.
    void rejectMetricWrite(double value);

    class Impl;
    std::unique_ptr<Impl> d;

    friend class Platform;
};

} // namespace Gui
} // namespace Firmir
