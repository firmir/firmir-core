#pragma once

#include <firmir/gui/font.hpp>
#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/string.hpp>

namespace Firmir {
namespace Gui {
namespace Controls {

class AbstractButton : public Item {
public:
    explicit AbstractButton(Object* parent = nullptr);
    ~AbstractButton() override;
    String text() const { return m_text; }
    void setText(String text);
    Font font() const { return m_font; }
    void setFont(Font font);
    bool isPressed() const { return m_pressed; }
    bool isChecked() const { return m_checked; }
    void setChecked(bool checked);
    bool checkable() const { return m_checkable; }
    void setCheckable(bool checkable) { auto lock = lockScene(); m_checkable = checkable; }
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
    String m_text;
    Font m_font{kDefaultFontFamily, 16};
    RasterizedText m_label;
    std::uint64_t m_labelGeneration = 0;
    bool m_pressed = false;
    bool m_checked = false;
    bool m_checkable = false;
};

class Button : public AbstractButton {
public:
    explicit Button(Object* parent = nullptr);
    bool isFlat() const { return m_flat; }
    void setFlat(bool flat) { auto lock = lockScene(); m_flat = flat; update(); }
protected:
    void paint(Painter& painter) override;
private:
    bool m_flat = false;
};

class CheckBox : public AbstractButton {
public:
    enum class CheckState { Unchecked, PartiallyChecked, Checked };
    explicit CheckBox(Object* parent = nullptr);
    CheckState checkState() const { return m_checkState; }
    void setCheckState(CheckState state);
    bool tristate() const { return m_tristate; }
    void setTristate(bool enabled) { auto lock = lockScene(); m_tristate = enabled; }
protected:
    void paint(Painter& painter) override;
    void toggle() override;
private:
    CheckState m_checkState = CheckState::Unchecked;
    bool m_tristate = false;
};

class RadioButton : public AbstractButton {
public:
    explicit RadioButton(Object* parent = nullptr);
    bool autoExclusive() const { return m_autoExclusive; }
    void setAutoExclusive(bool enabled) { auto lock = lockScene(); m_autoExclusive = enabled; }
protected:
    void paint(Painter& painter) override;
    void toggle() override;
private:
    bool m_autoExclusive = true;
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
