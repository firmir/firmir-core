#pragma once

// Pure selection logic behind the backend environment variables of
// firmir-gui:
//
//   FIRMIR_GUI_DISPLAY  — pin one display backend (headless, wayland, drm,
//                         eglfs), no fallback;
//   FIRMIR_GUI_INPUT    — pin one input backend (headless, wayland, evdev);
//   FIRMIR_GUI_BACKENDS — ordered, comma-separated list of backend names to
//                         try, e.g. "drm,eglfs": the first display-capable
//                         name whose plugin loads provides the display, the
//                         first input-capable one provides input.
//
// The helpers are free of dlopen() and device probing so the rules can be
// unit-tested; Platform turns the returned names into loaded plugins.

#include <string>
#include <vector>

namespace Firmir {
namespace Gui {
namespace detail {

/// True for the names a display backend can have: headless, wayland, drm,
/// eglfs.
bool isDisplayBackendName(const std::string& name);

/// True for the names an input backend can have: headless, wayland (the
/// Wayland plugin also provides input), evdev.
bool isInputBackendName(const std::string& name);

/// True for the display backends that drive real hardware and therefore need a
/// separate input backend (drm, eglfs).
bool isHardwareDisplayBackend(const std::string& name);

/// The list used when FIRMIR_GUI_BACKENDS is unset:
/// "wayland,eglfs,drm,evdev,headless" — hardware backends first, evdev pairs
/// with them, headless keeps GUI-less runs (and CI) working.
const std::vector<std::string>& defaultBackendList();

/// Splits a comma-separated backend list: items are trimmed, lower-cased,
/// empty ones dropped and duplicates removed (the first occurrence wins).
/// Unknown names are kept so the caller can report them.
std::vector<std::string> parseBackendList(const std::string& value);

/// True when @p name can work here at all: wayland needs a connectable
/// compositor socket, drm and eglfs need a /dev/dri/cardN node; headless
/// always can.
bool displayBackendAvailable(const std::string& name, bool waylandAvailable,
                            bool drmAvailable);

/// Display backends to try, in order. @p displayName pins a single backend and
/// wins over everything else (no availability filtering: the pin is an
/// explicit choice); otherwise the display-capable names of @p requested (or
/// of defaultBackendList() when @p requested is empty) are used in their order:
/// input-only names are ignored, names whose device is absent are reported
/// through @p skipped and the next one is tried. An empty result means the list
/// named display backends that cannot work here — the caller reports that
/// instead of silently running another backend.
std::vector<std::string> displayBackendCandidates(
    const std::string& displayName, const std::vector<std::string>& requested,
    bool waylandAvailable, bool drmAvailable,
    std::vector<std::string>* skipped = nullptr);

/// Input backends to try, in order. @p inputName pins a single backend;
/// otherwise the input-capable names of @p requested (or of
/// defaultBackendList()) are used, with two additions: the selected display
/// plugin's own input factory comes first when it is not a hardware backend
/// (Wayland and headless provide input themselves), and evdev is always tried
/// for the hardware backends drm/eglfs — it works alongside them, never
/// instead of them. headless is the final fallback.
std::vector<std::string> inputBackendCandidates(
    const std::string& inputName, const std::vector<std::string>& requested,
    const std::string& displayName, bool displayProvidesInput);

} // namespace detail
} // namespace Gui
} // namespace Firmir
