#include <firmir/gui/backend.hpp>
#include <firmir/gui/screen.hpp>

namespace Firmir {
namespace Gui {

class Screen::Impl {
public:
    explicit Impl(PlatformScreen* backend) : backend(backend) {}
    PlatformScreen* backend;
};

Screen::Screen(PlatformScreen* backend, Object* parent)
    : Object(parent), d(std::make_unique<Impl>(backend)) {}

Screen::~Screen() = default;

std::string Screen::name() const { return d->backend->name(); }

Size Screen::size() const { return d->backend->size(); }

} // namespace Gui
} // namespace Firmir
