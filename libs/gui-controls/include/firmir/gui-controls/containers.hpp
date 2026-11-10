#pragma once

#include <firmir/gui-controls/abstractbutton.hpp>
#include <firmir/gui/flickable.hpp>

#include <memory>
#include <vector>

namespace Firmir {
namespace Gui {
namespace Controls {

class ScrollView : public Item {
public:
    explicit ScrollView(Object* parent = nullptr);
    ~ScrollView() override;
    Item* contentItem() const;
    Flickable* flickable() const;
    void setContentSize(float width, float height);
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class SplitView : public Item {
public:
    enum class Orientation { Horizontal, Vertical };
    explicit SplitView(Object* parent = nullptr);
    ~SplitView() override;
    Orientation orientation() const;
    void setOrientation(Orientation orientation);
    void addItem(Item* item);
    int count() const;
    Item* itemAt(int index) const;
    void setSizes(const std::vector<float>& sizes);
    std::vector<float> sizes() const;
private:
    friend class SplitHandle;
    void layout();
    void moveHandle(int index, float delta);

    class Impl;
    std::unique_ptr<Impl> d;
};

class StackView : public Item {
public:
    explicit StackView(Object* parent = nullptr);
    ~StackView() override;
    int depth() const;
    Item* currentItem() const;
    void push(Item* item);
    Item* pop();
    void replace(Item* item);
    void clear();
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class SwipeView : public Item {
public:
    enum class Orientation { Horizontal, Vertical };
    explicit SwipeView(Object* parent = nullptr);
    ~SwipeView() override;
    void addItem(Item* item);
    int count() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
    Item* currentItem() const;
    Item* itemAt(int index) const;
    bool interactive() const;
    void setInteractive(bool enabled);
protected:
    void mouseEvent(MouseEvent& event) override;
    void touchEvent(TouchEvent& event) override;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

class TabBar : public Item {
public:
    explicit TabBar(Object* parent = nullptr);
    ~TabBar() override;
    void addTab(String text);
    int count() const;
    int currentIndex() const;
    void setCurrentIndex(int index);
    String textAt(int index) const;
    Button* tabAt(int index) const;
    static constexpr auto signalCurrentIndexChanged = "currentIndexChanged";
private:
    void layout();

    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Controls
} // namespace Gui
} // namespace Firmir
