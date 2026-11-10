#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>

#include <memory>

namespace Firmir {
namespace Gui {
namespace Controls {

class BusyIndicator : public Item {
public:
    explicit BusyIndicator(Object* parent = nullptr);
    ~BusyIndicator() override;
    bool running() const;
    void setRunning(bool running);
protected:
    void paint(Painter& painter) override;
    void timerEvent(int timerId) override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class ProgressBar : public Item {
public:
    explicit ProgressBar(Object* parent = nullptr);
    ~ProgressBar() override;
    float value() const;
    void setValue(float value);
    float from() const;
    void setFrom(float value);
    float to() const;
    void setTo(float value);
    float visualPosition() const;
    bool indeterminate() const;
    void setIndeterminate(bool indeterminate);
protected:
    void paint(Painter& painter) override;
    void timerEvent(int timerId) override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Controls
} // namespace Gui
} // namespace Firmir
