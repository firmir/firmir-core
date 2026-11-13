#include <firmir/gui/detail/backendselection.hpp>

#include <algorithm>
#include <cctype>

namespace Firmir {
namespace Gui {
namespace detail {

namespace {

/// Trims @p value and lower-cases it; backend names are case-insensitive.
std::string normalize(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    std::string result = value.substr(begin, end - begin);
    for (char& c : result)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}

/// Appends @p name unless it is already present: the first mention wins.
void appendUnique(std::vector<std::string>& names, const std::string& name) {
    for (const std::string& existing : names)
        if (existing == name)
            return;
    names.push_back(name);
}

/// Inserts @p name at the front unless it is already present.
void prependUnique(std::vector<std::string>& names, const std::string& name) {
    for (const std::string& existing : names)
        if (existing == name)
            return;
    names.insert(names.begin(), name);
}

/// Moves @p name to the front, adding it when missing.
void moveToFront(std::vector<std::string>& names, const std::string& name) {
    names.erase(std::remove(names.begin(), names.end(), name), names.end());
    names.insert(names.begin(), name);
}

} // namespace

bool isDisplayBackendName(const std::string& name) {
    return name == "headless" || name == "wayland" || name == "drm" || name == "eglfs";
}

bool isInputBackendName(const std::string& name) {
    return name == "headless" || name == "wayland" || name == "evdev";
}

bool isHardwareDisplayBackend(const std::string& name) {
    return name == "drm" || name == "eglfs";
}

const std::vector<std::string>& defaultBackendList() {
    static const std::vector<std::string> names{
        "wayland", "eglfs", "drm", "evdev", "headless"};
    return names;
}

std::vector<std::string> parseBackendList(const std::string& value) {
    std::vector<std::string> names;
    std::string current;
    for (const char c : value) {
        if (c != ',') {
            current += c;
            continue;
        }
        const std::string name = normalize(current);
        if (!name.empty())
            appendUnique(names, name);
        current.clear();
    }
    const std::string last = normalize(current);
    if (!last.empty())
        appendUnique(names, last);
    return names;
}

bool displayBackendAvailable(const std::string& name, bool waylandAvailable,
                            bool drmAvailable) {
    if (name == "wayland")
        return waylandAvailable;
    if (name == "drm" || name == "eglfs")
        return drmAvailable;
    return true;
}

std::vector<std::string> displayBackendCandidates(
    const std::string& displayName, const std::vector<std::string>& requested,
    bool waylandAvailable, bool drmAvailable,
    std::vector<std::string>* skipped) {
    std::vector<std::string> candidates;
    if (!displayName.empty()) {
        candidates.push_back(displayName);
        return candidates;
    }
    const bool useDefault = requested.empty();
    const std::vector<std::string>& names = useDefault ? defaultBackendList() : requested;
    bool listHasDisplay = false;
    for (const std::string& name : names) {
        if (!isDisplayBackendName(name))
            continue;
        listHasDisplay = true;
        if (!displayBackendAvailable(name, waylandAvailable, drmAvailable)) {
            if (skipped)
                skipped->push_back(name);
            continue;
        }
        appendUnique(candidates, name);
    }
    if (!candidates.empty())
        return candidates;
    if (!useDefault && !listHasDisplay) // only input backends listed
        return displayBackendCandidates("", {}, waylandAvailable, drmAvailable, skipped);
    if (useDefault) // defensive: headless is always built and always available
        appendUnique(candidates, "headless");
    return candidates;
}

std::vector<std::string> inputBackendCandidates(
    const std::string& inputName, const std::vector<std::string>& requested,
    const std::string& displayName, bool displayProvidesInput) {
    std::vector<std::string> candidates;
    if (!inputName.empty()) {
        candidates.push_back(inputName);
        return candidates;
    }
    const std::vector<std::string>& names =
        requested.empty() ? defaultBackendList() : requested;
    for (const std::string& name : names) {
        if (!isInputBackendName(name))
            continue;
        // The Wayland input backend belongs to that compositor connection: it
        // only makes sense when Wayland is the display backend as well.
        if (name == "wayland" && displayName != "wayland")
            continue;
        appendUnique(candidates, name);
    }
    // The wayland input backend belongs to that compositor connection and is
    // the natural pairing for a Wayland display: it only applies then.
    const bool ownInput = displayProvidesInput && !isHardwareDisplayBackend(displayName) &&
                          !displayName.empty();
    if (isHardwareDisplayBackend(displayName)) {
        // evdev works alongside the hardware backends: always try it with drm
        // or eglfs, even when the list does not name it.
        prependUnique(candidates, "evdev");
    } else if (ownInput && displayName == "headless") {
        // No real output: injecting device events makes no sense, so the
        // headless plugin's own input factory wins over listed evdev.
        moveToFront(candidates, "headless");
    } else if (ownInput) {
        // Wayland display: its own factory is the default, but an explicitly
        // listed input backend keeps its place.
        prependUnique(candidates, displayName);
    }
    appendUnique(candidates, "headless");
    return candidates;
}

} // namespace detail
} // namespace Gui
} // namespace Firmir
