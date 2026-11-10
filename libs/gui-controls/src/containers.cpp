#include <firmir/gui-controls/containers.hpp>
#include <algorithm>
#include <cmath>

namespace Firmir {
namespace Gui {
namespace Controls {

class SplitHandle;

class ScrollView::Impl {
public:
    Flickable* flickable = nullptr;
    Item* scrollBar = nullptr;
};

class SplitView::Impl {
public:
    Orientation orientation = Orientation::Horizontal;
    std::vector<Item*> items;
    std::vector<SplitHandle*> handles;
    std::vector<float> sizes;
    float handleSize = 6;
};

class StackView::Impl {
public:
    std::vector<Item*> stack;
};

class SwipeView::Impl {
public:
    std::vector<Item*> pages;
    Orientation orientation = Orientation::Horizontal;
    Point pressPosition;
    int currentIndex = -1;
    bool interactive = true;
    bool pressed = false;
};

class TabBar::Impl {
public:
    std::vector<Button*> tabs;
    int currentIndex = -1;
};
namespace {

class ScrollBarIndicator : public Item {
public:
    ScrollBarIndicator(Object* parent, Flickable* flickable) : Item(parent), m_flickable(flickable) {}
protected:
    void paint(Painter& painter) override {
        const float content = m_flickable->contentHeight();
        const float viewport = m_flickable->height();
        if (content <= viewport || viewport <= 0) return;
        const float radius = width() / 2.0f;
        painter.fillRoundedRect(RectF{0, 0, width(), height()}, radius,
                                Color::rgba(25, 30, 38, 160));
        const float barHeight = std::max(20.0f, height() * viewport / content);
        const float maximum = content - viewport;
        const float y = maximum > 0 ? (height() - barHeight) * m_flickable->contentY() / maximum : 0;
        painter.fillRoundedRect(RectF{1, y, std::max(1.0f, width() - 2), barHeight},
                                radius - 1.0f, Color::rgba(100, 180, 235, 220));
    }
private:
    Flickable* m_flickable;
};

} // namespace

namespace {

/// contentWidth/contentHeight registered properties, forwarded to the inner
/// flickable (ScrollView::setContentSize writes both at once).
void registerScrollViewProperties(ScrollView* self) {
    self->registerPropertyImpl("contentWidth", typeid(float),
        [](const Object* o) -> std::any {
            return static_cast<const ScrollView*>(o)->flickable()->contentWidth(); },
        [](Object* o, const std::any& v) {
            auto* sv = static_cast<ScrollView*>(o);
            const float w = Firmir::detail::anyConvert<float>(v);
            sv->setContentSize(w, sv->flickable()->contentHeight()); });
    self->registerPropertyImpl("contentHeight", typeid(float),
        [](const Object* o) -> std::any {
            return static_cast<const ScrollView*>(o)->flickable()->contentHeight(); },
        [](Object* o, const std::any& v) {
            auto* sv = static_cast<ScrollView*>(o);
            const float h = Firmir::detail::anyConvert<float>(v);
            sv->setContentSize(sv->flickable()->contentWidth(), h); });
}

} // namespace

ScrollView::ScrollView(Object* parent)
    : Item(parent), d(std::make_unique<Impl>()) {
    d->flickable = new Flickable(this);
    d->scrollBar = new ScrollBarIndicator(this, d->flickable);
    d->flickable->fill(this);
    d->scrollBar->setWidth(7);
    d->scrollBar->setAnchorRight({this, AnchorLine::Right});
    d->scrollBar->setAnchorTop({this, AnchorLine::Top});
    d->scrollBar->setAnchorBottom({this, AnchorLine::Bottom});
    // Keep the scrollbar in sync while the content moves (drag, wheel, flick).
    d->flickable->connect<std::string, std::any>(
        this, signalPropertyChanged,
        [this](const std::string& name, const std::any&) {
            if (name == "contentY" || name == "contentX")
                d->scrollBar->update();
        },
        ConnectionMode::Direct);
    registerScrollViewProperties(this);
}
ScrollView::~ScrollView() = default;
Item* ScrollView::contentItem() const { return d->flickable->contentItem(); }
Flickable* ScrollView::flickable() const { return d->flickable; }
void ScrollView::setContentSize(float width, float height) {
    auto sceneLock = lockScene();
    d->flickable->setContentWidth(width);
    d->flickable->setContentHeight(height);
    d->scrollBar->update();
}

class SplitHandle : public Item {
public:
    SplitHandle(SplitView* owner, int index) : Item(owner), m_owner(owner), m_index(index) { setZ(1); }
protected:
    void paint(Painter& painter) override {
        painter.fillRect(RectF{0, 0, width(), height()}, Color::rgb(80, 105, 130));
    }
    void mouseEvent(MouseEvent& event) override {
        if (event.action == MouseEvent::Action::Press && event.button == 1) {
            m_last = event.pos; event.accepted = true;
        } else if (event.action == MouseEvent::Action::Move && event.button == 0) {
            const float delta = m_owner->orientation() == SplitView::Orientation::Horizontal
                ? event.pos.x - m_last.x : event.pos.y - m_last.y;
            m_owner->moveHandle(m_index, delta);
            event.accepted = true;
        } else if (event.action == MouseEvent::Action::Release) {
            event.accepted = true;
        }
    }
private:
    SplitView* m_owner;
    int m_index;
    Point m_last;
};

SplitView::SplitView(Object* parent)
    : Item(parent), d(std::make_unique<Impl>()) {
    registerPropertyImpl("orientation", typeid(String),
        [](const Object* o) -> std::any {
            return String(static_cast<const SplitView*>(o)->orientation() ==
                                  Orientation::Horizontal ? "Horizontal" : "Vertical"); },
        [](Object* o, const std::any& v) {
            const std::string s = Firmir::detail::anyConvert<Firmir::String>(v).toStdString();
            static_cast<SplitView*>(o)->setOrientation(
                s == "Vertical" ? Orientation::Vertical : Orientation::Horizontal); });
}
SplitView::~SplitView() = default;
SplitView::Orientation SplitView::orientation() const { return d->orientation; }
int SplitView::count() const { return static_cast<int>(d->items.size()); }
std::vector<float> SplitView::sizes() const { return d->sizes; }
void SplitView::setOrientation(Orientation orientation) {
    auto sceneLock = lockScene();
    d->orientation = orientation; layout(); notifyPropertyChanged("orientation");
}
void SplitView::addItem(Item* item) {
    auto sceneLock = lockScene();
    if (!item) return;
    item->setParent(this);
    d->items.push_back(item);
    d->sizes.push_back(160);
    if (d->items.size() > 1)
        d->handles.push_back(new SplitHandle(this, static_cast<int>(d->items.size()) - 2));
    layout();
}
Item* SplitView::itemAt(int index) const {
    return index >= 0 && index < count() ? d->items[static_cast<std::size_t>(index)] : nullptr;
}
void SplitView::setSizes(const std::vector<float>& sizes) {
    auto sceneLock = lockScene();
    d->sizes = sizes;
    d->sizes.resize(d->items.size(), 160);
    layout();
}
void SplitView::layout() {
    auto sceneLock = lockScene();
    if (d->items.empty()) return;
    const float available = std::max(1.0f,
        (d->orientation == Orientation::Horizontal ? width() : height()) -
        d->handleSize * d->handles.size());
    float total = 0;
    for (float size : d->sizes) total += std::max(20.0f, size);
    float position = 0;
    for (std::size_t i = 0; i < d->items.size(); ++i) {
        const float size = available * std::max(20.0f, d->sizes[i]) / total;
        if (d->orientation == Orientation::Horizontal)
            d->items[i]->setGeometry(position, 0, size, height());
        else
            d->items[i]->setGeometry(0, position, width(), size);
        position += size;
        if (i < d->handles.size()) {
            if (d->orientation == Orientation::Horizontal)
                d->handles[i]->setGeometry(position, 0, d->handleSize, height());
            else
                d->handles[i]->setGeometry(0, position, width(), d->handleSize);
            position += d->handleSize;
        }
    }
}
void SplitView::moveHandle(int index, float delta) {
    auto sceneLock = lockScene();
    if (index < 0 || static_cast<std::size_t>(index + 1) >= d->sizes.size()) return;
    const auto i = static_cast<std::size_t>(index);
    const float old = d->sizes[i];
    d->sizes[i] = std::max(30.0f, d->sizes[i] + delta);
    d->sizes[i + 1] = std::max(30.0f, d->sizes[i + 1] - (d->sizes[i] - old));
    layout();
}

StackView::StackView(Object* parent)
    : Item(parent), d(std::make_unique<Impl>()) {
    setClip(true);
    registerPropertyImpl("depth", typeid(int),
        [](const Object* o) -> std::any { return static_cast<const StackView*>(o)->depth(); },
        [](Object*, const std::any&) { throw std::invalid_argument("depth is read-only"); });
}
StackView::~StackView() = default;
int StackView::depth() const { return static_cast<int>(d->stack.size()); }
Item* StackView::currentItem() const { return d->stack.empty() ? nullptr : d->stack.back(); }
void StackView::push(Item* item) {
    auto sceneLock = lockScene();
    if (!item) return;
    if (currentItem()) currentItem()->setVisible(false);
    item->setParent(this);
    item->fill(this);
    item->setVisible(true);
    d->stack.push_back(item);
    notifyPropertyChanged("depth");
    notifyPropertyChanged("currentItem");
}
Item* StackView::pop() {
    auto sceneLock = lockScene();
    if (d->stack.empty()) return nullptr;
    Item* removed = d->stack.back();
    d->stack.pop_back();
    removed->setVisible(false);
    if (currentItem()) currentItem()->setVisible(true);
    notifyPropertyChanged("depth");
    notifyPropertyChanged("currentItem");
    return removed;
}
void StackView::replace(Item* item) {
    auto sceneLock = lockScene(); clear(); push(item); }
void StackView::clear() {
    auto sceneLock = lockScene();
    for (Item* item : d->stack) item->setVisible(false);
    d->stack.clear();
    notifyPropertyChanged("depth");
    notifyPropertyChanged("currentItem");
}

SwipeView::SwipeView(Object* parent)
    : Item(parent), d(std::make_unique<Impl>()) {
    setClip(true);
    registerProperty<int>("currentIndex", &SwipeView::currentIndex, &SwipeView::setCurrentIndex);
    registerProperty<bool>("interactive", &SwipeView::interactive, &SwipeView::setInteractive);
    registerPropertyImpl("count", typeid(int),
        [](const Object* o) -> std::any { return static_cast<const SwipeView*>(o)->count(); },
        [](Object*, const std::any&) { throw std::invalid_argument("count is read-only"); });
}
SwipeView::~SwipeView() = default;
int SwipeView::count() const { return static_cast<int>(d->pages.size()); }
int SwipeView::currentIndex() const { return d->currentIndex; }
bool SwipeView::interactive() const { return d->interactive; }
void SwipeView::setInteractive(bool enabled) { auto lock = lockScene(); d->interactive = enabled; }
void SwipeView::addItem(Item* item) {
    auto sceneLock = lockScene();
    if (!item) return;
    item->setParent(this);
    item->fill(this);
    d->pages.push_back(item);
    if (d->currentIndex < 0) setCurrentIndex(0);
    else item->setVisible(false);
    notifyPropertyChanged("count");
}
Item* SwipeView::itemAt(int index) const {
    return index >= 0 && index < count() ? d->pages[static_cast<std::size_t>(index)] : nullptr;
}
Item* SwipeView::currentItem() const { return itemAt(d->currentIndex); }
void SwipeView::setCurrentIndex(int index) {
    auto sceneLock = lockScene();
    if (d->pages.empty() || index < 0 || index >= count() || index == d->currentIndex) return;
    d->currentIndex = index;
    for (int i = 0; i < count(); ++i) d->pages[static_cast<std::size_t>(i)]->setVisible(i == index);
    update();
    notifyPropertyChanged("currentIndex");
}
void SwipeView::mouseEvent(MouseEvent& event) {
    if (!d->interactive) return;
    if (event.action == MouseEvent::Action::Press && event.button == 1) {
        d->pressed = true; d->pressPosition = event.pos; event.accepted = true;
    } else if (event.action == MouseEvent::Action::Release && d->pressed) {
        d->pressed = false;
        const float delta = d->orientation == Orientation::Horizontal
            ? event.pos.x - d->pressPosition.x : event.pos.y - d->pressPosition.y;
        if (delta < -30) setCurrentIndex(std::min(count() - 1, d->currentIndex + 1));
        else if (delta > 30) setCurrentIndex(std::max(0, d->currentIndex - 1));
        event.accepted = true;
    }
}
void SwipeView::touchEvent(TouchEvent& event) {
    if (event.points.empty()) return;
    MouseEvent mouse;
    mouse.pos = event.points.front().pos; mouse.button = 1;
    mouse.action = event.action == TouchEvent::Action::Begin ? MouseEvent::Action::Press
        : event.action == TouchEvent::Action::End ? MouseEvent::Action::Release : MouseEvent::Action::Move;
    mouseEvent(mouse); event.accepted = mouse.accepted;
}

TabBar::TabBar(Object* parent)
    : Item(parent), d(std::make_unique<Impl>()) {
    setImplicitHeight(40);
    registerProperty<int>("currentIndex", &TabBar::currentIndex, &TabBar::setCurrentIndex);
    registerPropertyImpl("count", typeid(int),
        [](const Object* o) -> std::any { return static_cast<const TabBar*>(o)->count(); },
        [](Object*, const std::any&) { throw std::invalid_argument("count is read-only"); });
}
TabBar::~TabBar() = default;
int TabBar::count() const { return static_cast<int>(d->tabs.size()); }
int TabBar::currentIndex() const { return d->currentIndex; }
void TabBar::addTab(String text) {
    auto sceneLock = lockScene();
    auto* button = new Button(this);
    button->setCheckable(true);
    button->setText(std::move(text));
    button->connect<>(button, AbstractButton::signalClicked, [this, button] {
        const auto found = std::find(d->tabs.begin(), d->tabs.end(), button);
        if (found != d->tabs.end()) setCurrentIndex(static_cast<int>(found - d->tabs.begin()));
    }, ConnectionMode::Direct);
    d->tabs.push_back(button);
    if (d->currentIndex < 0) setCurrentIndex(0);
    layout();
    notifyPropertyChanged("count");
}
String TabBar::textAt(int index) const {
    auto* button = tabAt(index);
    return button ? button->text() : String();
}
Button* TabBar::tabAt(int index) const {
    return index >= 0 && index < count() ? d->tabs[static_cast<std::size_t>(index)] : nullptr;
}
void TabBar::setCurrentIndex(int index) {
    auto sceneLock = lockScene();
    if (index < 0 || index >= count()) return;
    d->currentIndex = index;
    for (int i = 0; i < count(); ++i) d->tabs[static_cast<std::size_t>(i)]->setChecked(i == index);
    notifyPropertyChanged("currentIndex");
    emitSignal(signalCurrentIndexChanged, index);
}
void TabBar::layout() {
    auto sceneLock = lockScene();
    if (d->tabs.empty()) return;
    const float tabWidth = (width() > 0 ? width() : 120.0f * d->tabs.size()) / d->tabs.size();
    for (std::size_t i = 0; i < d->tabs.size(); ++i)
        d->tabs[i]->setGeometry(i * tabWidth, 0, tabWidth, height() > 0 ? height() : 40);
}
} // namespace Controls
} // namespace Gui
} // namespace Firmir
