#include <firmir/gui/app.hpp>

#include <firmir/gui/window.hpp>
#include <firmir/thread.hpp>

namespace Firmir {
namespace Gui {

App::App(Window* window, Object* parent) : Object(parent), m_window(window) {}

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

} // namespace Gui
} // namespace Firmir
