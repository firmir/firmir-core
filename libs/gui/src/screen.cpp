#include <firmir/gui/backend.hpp>
#include <firmir/gui/screen.hpp>

#include <cmath>
#include <stdexcept>

namespace Firmir {
namespace Gui {

namespace {

/// Diagonal length of @p size; 0 when either side is not positive.
double diagonal(const Size& size) {
    if (size.width <= 0 || size.height <= 0)
        return 0.0;
    return std::hypot(static_cast<double>(size.width),
                      static_cast<double>(size.height));
}

/// Millimetres per inch (exact, by definition).
constexpr double kMillimetresPerInch = 25.4;

/// Plausible density range for a real panel. A backend that reports a bogus
/// physical size (some connectors report a 1x1 mm panel) would otherwise turn
/// every millimetre-based layout into nonsense, so such a report is treated as
/// "unknown" and the layout falls back to its default metrics.
constexpr double kMinPixelsPerInch = 24.0;
constexpr double kMaxPixelsPerInch = 1000.0;

} // namespace

class Screen::Impl {
public:
    explicit Impl(PlatformScreen* backend) : backend(backend) {}
    PlatformScreen* backend;
};

Screen::Screen(PlatformScreen* backend, Object* parent)
    : Object(parent), d(std::make_unique<Impl>(backend)) {
    // Read-only, like App::screenWidth: layout code reads them, and a write is
    // a bug rather than a value to store.
    registerProperty<double>("ppi", &Screen::ppi, &Screen::rejectMetricWrite);
    registerProperty<double>("ppcm", &Screen::ppcm, &Screen::rejectMetricWrite);
}

Screen::~Screen() = default;

std::string Screen::name() const { return d->backend->name(); }

Size Screen::size() const { return d->backend->size(); }

Size Screen::physicalSize() const { return d->backend->physicalSize(); }

double Screen::ppi() const {
    const Size mm = physicalSize();
    const double pixelDiagonal = diagonal(size());
    const double inchDiagonal = diagonal(mm) / kMillimetresPerInch;
    if (pixelDiagonal <= 0.0 || inchDiagonal <= 0.0)
        return 0.0;
    const double density = pixelDiagonal / inchDiagonal;
    if (density < kMinPixelsPerInch || density > kMaxPixelsPerInch)
        return 0.0;
    return density;
}

double Screen::ppcm() const {
    // Derived from ppi so the two can never disagree: one inch is 2.54 cm.
    const double density = this->ppi();
    return density > 0.0 ? density / 2.54 : 0.0;
}

void Screen::rejectMetricWrite(double) {
    throw std::invalid_argument(
        "Firmir::Gui::Screen: 'ppi'/'ppcm' are read-only");
}

} // namespace Gui
} // namespace Firmir
