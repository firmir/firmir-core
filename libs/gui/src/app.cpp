#include <firmir/gui/app.hpp>

#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/thread.hpp>

#include <stdexcept>
#include <vector>

namespace Firmir {
namespace Gui {

class App::Impl {
public:
    Window* window = nullptr;
    bool maximized = false;
};

App::App(Window* window, Object* parent) : Object(parent), d(std::make_unique<Impl>()) {
    d->window = window;
    registerProperty<double>("screenWidth", &App::screenWidth,
                             &App::rejectScreenSizeWrite);
    registerProperty<double>("screenHeight", &App::screenHeight,
                             &App::rejectScreenSizeWrite);
    Platform* platform = Platform::instance();
    platform->connect<Screen*>(this, Platform::signalScreenAdded,
                               [this](Screen*) { notifyScreenSizeChanged(); });
    platform->connect<Screen*>(this, Platform::signalScreenRemoved,
                               [this](Screen*) { notifyScreenSizeChanged(); });
}

App::~App() = default;

Window* App::window() const { return d->window; }
void App::show() {
    if (d->window)
        d->window->show();
}
void App::hide() {
    if (d->window)
        d->window->hide();
}
void App::quit() { Thread::mainThread()->quit(); }
void App::setTitle(String title) {
    if (d->window)
        d->window->setTitle(title.toStdString());
}

void App::showMinimized() {
    if (d->window)
        d->window->showMinimized();
}
void App::showNormal() {
    if (d->window) {
        d->window->showNormal();
        d->maximized = false;
    }
}
void App::showMaximized() {
    if (d->window) {
        d->window->showMaximized();
        d->maximized = true;
    }
}
void App::toggleMaximized() {
    if (d->maximized)
        showNormal();
    else
        showMaximized();
}
void App::close() {
    if (d->window)
        d->window->close();
}

double App::screenWidth() const {
    const auto screens = Platform::instance()->screens();
    return screens.empty() ? 0.0 : static_cast<double>(screens.front()->size().width);
}

double App::screenHeight() const {
    const auto screens = Platform::instance()->screens();
    return screens.empty() ? 0.0 : static_cast<double>(screens.front()->size().height);
}

void App::rejectScreenSizeWrite(double) {
    throw std::invalid_argument(
        "Firmir::Gui::App: 'screenWidth'/'screenHeight' are read-only");
}

void App::notifyScreenSizeChanged() {
    notifyPropertyChanged("screenWidth");
    notifyPropertyChanged("screenHeight");
}

} // namespace Gui
} // namespace Firmir
