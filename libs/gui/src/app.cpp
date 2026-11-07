#include <firmir/gui/app.hpp>

#include <firmir/gui/platform.hpp>
#include <firmir/gui/screen.hpp>
#include <firmir/gui/window.hpp>
#include <firmir/thread.hpp>

#include <stdexcept>
#include <vector>

namespace Firmir {
namespace Gui {

App::App(Window* window, Object* parent) : Object(parent), m_window(window) {
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

Window* App::window() const { return m_window; }
void App::show() {
    if (m_window)
        m_window->show();
}
void App::hide() {
    if (m_window)
        m_window->hide();
}
void App::quit() { Thread::mainThread()->quit(); }
void App::setTitle(String title) {
    if (m_window)
        m_window->setTitle(title.toStdString());
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
