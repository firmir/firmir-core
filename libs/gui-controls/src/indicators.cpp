#include <firmir/gui-controls/indicators.hpp>

#include <firmir/gui/theme.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Firmir {
namespace Gui {
namespace Controls {

class BusyIndicator::Impl {
public:
    bool running = true;
    int timer = 0;
    int phase = 0;
};

class ProgressBar::Impl {
public:
    float value = 0, from = 0, to = 1;
    bool indeterminate = false;
    int timer = 0;
    float phase = 0;
};

BusyIndicator::BusyIndicator(Object* parent) : Item(parent), d(std::make_unique<Impl>()) {
    setImplicitWidth(48); setImplicitHeight(48);
    registerProperty<bool>("running", &BusyIndicator::running, &BusyIndicator::setRunning);
    d->timer = startTimer(std::chrono::milliseconds(80));
}
BusyIndicator::~BusyIndicator() { if (d->timer) killTimer(d->timer); }
bool BusyIndicator::running() const { return d->running; }
void BusyIndicator::setRunning(bool running) {
    auto sceneLock = lockScene();
    if (d->running == running) return;
    d->running = running;
    if (running && !d->timer) d->timer = startTimer(std::chrono::milliseconds(80));
    else if (!running && d->timer) { killTimer(d->timer); d->timer = 0; }
    update();
    notifyPropertyChanged("running");
}
void BusyIndicator::timerEvent(int timerId) {
    auto sceneLock = lockScene();
    if (timerId == d->timer && d->running) { d->phase = (d->phase + 1) % 8; update(); }
}
void BusyIndicator::paint(Painter& painter) {
    const float radius = std::min(width(), height()) * 0.35f;
    const float cx = width() / 2, cy = height() / 2;
    for (int i = 0; i < 8; ++i) {
        const float angle = (i * 45 - 90) * 3.14159265358979323846f / 180.0f;
        const int alpha = d->running ? 35 + ((i + d->phase) % 8) * 28 : 80;
        painter.fillRoundedRect(RectF{cx + std::cos(angle) * radius - 3,
                                      cy + std::sin(angle) * radius - 3, 6, 6},
                                3.0f,
                                Color::rgba(80, 175, 235, static_cast<std::uint8_t>(alpha)));
    }
}

ProgressBar::ProgressBar(Object* parent) : Item(parent), d(std::make_unique<Impl>()) {
    setImplicitWidth(180); setImplicitHeight(12);
    registerProperty<float>("value", &ProgressBar::value, &ProgressBar::setValue);
    registerProperty<float>("from", &ProgressBar::from, &ProgressBar::setFrom);
    registerProperty<float>("to", &ProgressBar::to, &ProgressBar::setTo);
    registerProperty<bool>("indeterminate", &ProgressBar::indeterminate, &ProgressBar::setIndeterminate);
}
ProgressBar::~ProgressBar() { if (d->timer) killTimer(d->timer); }
float ProgressBar::value() const { return d->value; }
float ProgressBar::from() const { return d->from; }
float ProgressBar::to() const { return d->to; }
bool ProgressBar::indeterminate() const { return d->indeterminate; }
void ProgressBar::setValue(float value) {
    auto sceneLock = lockScene();
    const float bounded = std::clamp(value, std::min(d->from, d->to), std::max(d->from, d->to));
    if (d->value == bounded) return;
    d->value = bounded; update(); notifyPropertyChanged("value");
}
void ProgressBar::setFrom(float value) {
    auto sceneLock = lockScene();
    d->from = value; setValue(d->value); update(); notifyPropertyChanged("from");
}
void ProgressBar::setTo(float value) {
    auto sceneLock = lockScene();
    d->to = value; setValue(d->value); update(); notifyPropertyChanged("to");
}
float ProgressBar::visualPosition() const {
    return d->to == d->from ? 0 : std::clamp((d->value - d->from) / (d->to - d->from), 0.0f, 1.0f);
}
void ProgressBar::setIndeterminate(bool indeterminate) {
    auto sceneLock = lockScene();
    if (d->indeterminate == indeterminate) return;
    d->indeterminate = indeterminate;
    if (indeterminate && !d->timer) d->timer = startTimer(std::chrono::milliseconds(16));
    else if (!indeterminate && d->timer) { killTimer(d->timer); d->timer = 0; }
    update(); notifyPropertyChanged("indeterminate");
}
void ProgressBar::timerEvent(int timerId) {
    auto sceneLock = lockScene();
    if (timerId != d->timer || !d->indeterminate) return;
    d->phase += 0.015f;
    if (d->phase > 1) d->phase -= 1;
    update();
}
void ProgressBar::paint(Painter& painter) {
    const Theme* theme = Theme::instance();
    const float radius = std::min(height() / 2.0f,
                                  static_cast<float>(theme->controlRadius()));
    painter.fillRoundedRect(RectF{0, 0, width(), height()}, radius,
                            theme->darkSecondaryColor());
    if (d->indeterminate) {
        const float barWidth = std::max(12.0f, width() * 0.3f);
        const float x = d->phase * (width() + barWidth) - barWidth;
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
