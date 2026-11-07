#pragma once

#include <firmir/gui/item.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/string.hpp>

namespace Firmir {
namespace Gui {

/**
 * @brief Push button with a centered label (default font: Inter).
 *
 * Emits "clicked" on release inside the button (or Space/Enter) and exposes
 * text/color/textColor/fontSize/pressed through the property system. The
 * implicit size follows the label plus padding.
 */
class Button : public Item {
public:
    explicit Button(Object* parent = nullptr);
    ~Button() override;

    String text() const;
    void setText(String text);
    Color color() const;
    void setColor(Color color);
    Color textColor() const;
    void setTextColor(Color color);
    int fontSize() const;
    void setFontSize(int fontSize);
    bool isPressed() const;

    static constexpr auto signalClicked = "clicked";

protected:
    void paint(Painter& painter) override;
    void mouseEvent(MouseEvent& event) override;
    void touchEvent(TouchEvent& event) override;
    void keyEvent(KeyEvent& event) override;

private:
    void setPressed(bool pressed);
    void click();

    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Gui
} // namespace Firmir
