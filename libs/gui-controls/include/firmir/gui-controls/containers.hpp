#pragma once

#include <firmir/gui-controls/abstractbutton.hpp>
#include <firmir/gui/flickable.hpp>

#include <vector>

namespace Firmir {
namespace Gui {
namespace Controls {

class ScrollView : public Item {
public:
    explicit ScrollView(Object* parent = nullptr);
    ~ScrollView() override;
    Item* contentItem() const;
    Flickable* flickable() const { return m_flickable; }
    void setContentSize(float width, float height);
private:
    Flickable* m_flickable;
    Item* m_scrollBar;
};

class SplitView : public Item {
public:
    enum class Orientation { Horizontal, Vertical };
    explicit SplitView(Object* parent = nullptr);
    ~SplitView() override;
    Orientation orientation() const { return m_orientation; }
    void setOrientation(Orientation orientation);
    void addItem(Item* item);
    int count() const { return static_cast<int>(m_items.size()); }
    Item* itemAt(int index) const;
    void setSizes(const std::vector<float>& sizes);
    std::vector<float> sizes() const { return m_sizes; }
private:
    friend class SplitHandle;
    void layout();
    void moveHandle(int index, float delta);
    Orientation m_orientation = Orientation::Horizontal;
    std::vector<Item*> m_items;
    std::vector<class SplitHandle*> m_handles;
    std::vector<float> m_sizes;
    float m_handleSize = 6;
};

class StackView : public Item {
public:
    explicit StackView(Object* parent = nullptr);
    ~StackView() override;
    int depth() const { return static_cast<int>(m_stack.size()); }
    Item* currentItem() const { return m_stack.empty() ? nullptr : m_stack.back(); }
    void push(Item* item);
    Item* pop();
    void replace(Item* item);
    void clear();
private:
    std::vector<Item*> m_stack;
};

class SwipeView : public Item {
public:
    enum class Orientation { Horizontal, Vertical };
    explicit SwipeView(Object* parent = nullptr);
    ~SwipeView() override;
    void addItem(Item* item);
    int count() const { return static_cast<int>(m_pages.size()); }
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    Item* currentItem() const;
    Item* itemAt(int index) const;
    bool interactive() const { return m_interactive; }
    void setInteractive(bool enabled) { auto lock = lockScene(); m_interactive = enabled; }
protected:
    void mouseEvent(MouseEvent& event) override;
    void touchEvent(TouchEvent& event) override;
private:
    std::vector<Item*> m_pages;
    Orientation m_orientation = Orientation::Horizontal;
    Point m_pressPosition;
    int m_currentIndex = -1;
    bool m_interactive = true;
    bool m_pressed = false;
};

class TabBar : public Item {
public:
    explicit TabBar(Object* parent = nullptr);
    ~TabBar() override;
    void addTab(String text);
    int count() const { return static_cast<int>(m_tabs.size()); }
    int currentIndex() const { return m_currentIndex; }
    void setCurrentIndex(int index);
    String textAt(int index) const;
    Button* tabAt(int index) const;
    static constexpr auto signalCurrentIndexChanged = "currentIndexChanged";
private:
    void layout();
    std::vector<Button*> m_tabs;
    int m_currentIndex = -1;
};

} // namespace Controls
} // namespace Gui
} // namespace Firmir
