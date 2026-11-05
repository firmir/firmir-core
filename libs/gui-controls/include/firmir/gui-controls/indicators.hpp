#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>

namespace Firmir {
namespace Gui {
namespace Controls {

class BusyIndicator : public Item {
public:
    explicit BusyIndicator(Object* parent = nullptr);
    ~BusyIndicator() override;
    bool running() const { return m_running; }
    void setRunning(bool running);
protected:
    void paint(Painter& painter) override;
    void timerEvent(int timerId) override;
private:
    bool m_running = true;
    int m_timer = 0;
    int m_phase = 0;
};

class ProgressBar : public Item {
public:
    explicit ProgressBar(Object* parent = nullptr);
    ~ProgressBar() override;
    float value() const { return m_value; }
    void setValue(float value);
    float from() const { return m_from; }
    void setFrom(float value);
    float to() const { return m_to; }
    void setTo(float value);
    float visualPosition() const;
    bool indeterminate() const { return m_indeterminate; }
    void setIndeterminate(bool indeterminate);
protected:
    void paint(Painter& painter) override;
    void timerEvent(int timerId) override;
private:
    float m_value = 0, m_from = 0, m_to = 1;
    bool m_indeterminate = false;
    int m_timer = 0;
    float m_phase = 0;
};

} // namespace Controls
} // namespace Gui
} // namespace Firmir
