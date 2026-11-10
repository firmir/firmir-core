#pragma once

#include <firmir/gui/font.hpp>
#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/string.hpp>

#include <memory>

namespace Firmir {
namespace Gui {
namespace Controls {

class AbstractButton : public Item {
public:
    explicit AbstractButton(Object* parent = nullptr);
    ~AbstractButton() override;
    String text() const;
    void setText(String text);
    Font font() const;
    void setFont(Font font);
    bool isPressed() const;
    bool isChecked() const;
    void setChecked(bool checked);
    bool checkable() const;
    void setCheckable(bool checkable);
    void click();
    static constexpr auto signalClicked = "clicked";
    static constexpr auto signalToggled = "toggled";
protected:
    void paint(Painter& painter) override;
    void mouseEvent(MouseEvent& event) override;
    void touchEvent(TouchEvent& event) override;
    void keyEvent(KeyEvent& event) override;
    virtual void toggle();
    void drawLabel(Painter& painter, float x, float y, Color color);
    /// Label dimensions in pixels (native font metrics when available).
    void labelSize(float& w, float& h) const;
    int textScale() const;
private:
    void setPressed(bool pressed);
    void updateLabel();

    class Impl;
    std::unique_ptr<Impl> d;
};

class Button : public AbstractButton {
public:
    explicit Button(Object* parent = nullptr);
    ~Button() override;
    bool isFlat() const;
    void setFlat(bool flat);
protected:
    void paint(Painter& painter) override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class CheckBox : public AbstractButton {
public:
    enum class CheckState { Unchecked, PartiallyChecked, Checked };
    explicit CheckBox(Object* parent = nullptr);
    ~CheckBox() override;
    CheckState checkState() const;
    void setCheckState(CheckState state);
    bool tristate() const;
    void setTristate(bool enabled);
protected:
    void paint(Painter& painter) override;
    void toggle() override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class RadioButton : public AbstractButton {
public:
    explicit RadioButton(Object* parent = nullptr);
    ~RadioButton() override;
    bool autoExclusive() const;
    void setAutoExclusive(bool enabled);
protected:
    void paint(Painter& painter) override;
    void toggle() override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class Switch : public AbstractButton {
public:
    explicit Switch(Object* parent = nullptr);
protected:
    void paint(Painter& painter) override;
};

} // namespace Controls
} // namespace Gui
} // namespace Firmir
