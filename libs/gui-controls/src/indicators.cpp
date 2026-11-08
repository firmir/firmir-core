#include <firmir/gui-controls/indicators.hpp>

#include <firmir/gui/theme.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Firmir {
namespace Gui {
namespace Controls {

BusyIndicator::BusyIndicator(Object* parent) : Item(parent) {
    setImplicitWidth(48); setImplicitHeight(48);
    registerProperty<bool>("running", &BusyIndicator::running, &BusyIndicator::setRunning);
    m_timer = startTimer(std::chrono::milliseconds(80));
}
BusyIndicator::~BusyIndicator() { if (m_timer) killTimer(m_timer); }
void BusyIndicator::setRunning(bool running) {
    auto sceneLock = lockScene();
    if (m_running == running) return;
    m_running = running;
    if (running && !m_timer) m_timer = startTimer(std::chrono::milliseconds(80));
    else if (!running && m_timer) { killTimer(m_timer); m_timer = 0; }
    update();
    notifyPropertyChanged("running");
}
void BusyIndicator::timerEvent(int timerId) {
    auto sceneLock = lockScene();
    if (timerId == m_timer && m_running) { m_phase = (m_phase + 1) % 8; update(); }
}
void BusyIndicator::paint(Painter& painter) {
    const float radius = std::min(width(), height()) * 0.35f;
    const float cx = width() / 2, cy = height() / 2;
    for (int i = 0; i < 8; ++i) {
        const float angle = (i * 45 - 90) * 3.14159265358979323846f / 180.0f;
        const int alpha = m_running ? 35 + ((i + m_phase) % 8) * 28 : 80;
        painter.fillRoundedRect(RectF{cx + std::cos(angle) * radius - 3,
                                      cy + std::sin(angle) * radius - 3, 6, 6},
                                3.0f,
                                Color::rgba(80, 175, 235, static_cast<std::uint8_t>(alpha)));
    }
}

ProgressBar::ProgressBar(Object* parent) : Item(parent) {
    setImplicitWidth(180); setImplicitHeight(12);
    registerProperty<float>("value", &ProgressBar::value, &ProgressBar::setValue);
    registerProperty<float>("from", &ProgressBar::from, &ProgressBar::setFrom);
    registerProperty<float>("to", &ProgressBar::to, &ProgressBar::setTo);
    registerProperty<bool>("indeterminate", &ProgressBar::indeterminate, &ProgressBar::setIndeterminate);
}
ProgressBar::~ProgressBar() { if (m_timer) killTimer(m_timer); }
void ProgressBar::setValue(float value) {
    auto sceneLock = lockScene();
    const float bounded = std::clamp(value, std::min(m_from, m_to), std::max(m_from, m_to));
    if (m_value == bounded) return;
    m_value = bounded; update(); notifyPropertyChanged("value");
}
void ProgressBar::setFrom(float value) {
    auto sceneLock = lockScene();
    m_from = value; setValue(m_value); update(); notifyPropertyChanged("from");
}
void ProgressBar::setTo(float value) {
    auto sceneLock = lockScene();
    m_to = value; setValue(m_value); update(); notifyPropertyChanged("to");
}
float ProgressBar::visualPosition() const {
    return m_to == m_from ? 0 : std::clamp((m_value - m_from) / (m_to - m_from), 0.0f, 1.0f);
}
void ProgressBar::setIndeterminate(bool indeterminate) {
    auto sceneLock = lockScene();
    if (m_indeterminate == indeterminate) return;
    m_indeterminate = indeterminate;
    if (indeterminate && !m_timer) m_timer = startTimer(std::chrono::milliseconds(16));
    else if (!indeterminate && m_timer) { killTimer(m_timer); m_timer = 0; }
    update(); notifyPropertyChanged("indeterminate");
}
void ProgressBar::timerEvent(int timerId) {
    auto sceneLock = lockScene();
    if (timerId != m_timer || !m_indeterminate) return;
    m_phase += 0.015f;
    if (m_phase > 1) m_phase -= 1;
    update();
}
void ProgressBar::paint(Painter& painter) {
    const Theme* theme = Theme::instance();
    const float radius = std::min(height() / 2.0f,
                                  static_cast<float>(theme->controlRadius()));
    painter.fillRoundedRect(RectF{0, 0, width(), height()}, radius,
                            theme->darkSecondaryColor());
    if (m_indeterminate) {
        const float barWidth = std::max(12.0f, width() * 0.3f);
        const float x = m_phase * (width() + barWidth) - barWidth;
        painter.save();
        painter.clipRect(RectF{0, 0, width(), height()});
        painter.fillRoundedRect(RectF{x, 0, barWidth, height()}, radius,
                                theme->highlightColor());
        painter.restore();
    } else {
        painter.fillRoundedRect(
            RectF{0, 0, width() * visualPosition(), height()}, radius,
            theme->highlightColor());
    }
}

} // namespace Controls
} // namespace Gui
} // namespace Firmir
