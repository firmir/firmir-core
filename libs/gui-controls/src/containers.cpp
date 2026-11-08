#include <firmir/gui-controls/containers.hpp>
#include <algorithm>
#include <cmath>

namespace Firmir {
namespace Gui {
namespace Controls {
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
    : Item(parent), m_flickable(new Flickable(this)),
      m_scrollBar(new ScrollBarIndicator(this, m_flickable)) {
    m_flickable->fill(this);
    m_scrollBar->setWidth(7);
    m_scrollBar->setAnchorRight({this, AnchorLine::Right});
    m_scrollBar->setAnchorTop({this, AnchorLine::Top});
    m_scrollBar->setAnchorBottom({this, AnchorLine::Bottom});
    // Keep the scrollbar in sync while the content moves (drag, wheel, flick).
    m_flickable->connect<std::string, std::any>(
        this, signalPropertyChanged,
        [this](const std::string& name, const std::any&) {
            if (name == "contentY" || name == "contentX")
                m_scrollBar->update();
        },
        ConnectionMode::Direct);
    registerScrollViewProperties(this);
}
ScrollView::~ScrollView() = default;
Item* ScrollView::contentItem() const { return m_flickable->contentItem(); }
void ScrollView::setContentSize(float width, float height) {
    auto sceneLock = lockScene();
    m_flickable->setContentWidth(width);
    m_flickable->setContentHeight(height);
    m_scrollBar->update();
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

SplitView::SplitView(Object* parent) : Item(parent) {
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
void SplitView::setOrientation(Orientation orientation) {
    auto sceneLock = lockScene();
    m_orientation = orientation; layout(); notifyPropertyChanged("orientation");
}
void SplitView::addItem(Item* item) {
    auto sceneLock = lockScene();
    if (!item) return;
    item->setParent(this);
    m_items.push_back(item);
    m_sizes.push_back(160);
    if (m_items.size() > 1)
        m_handles.push_back(new SplitHandle(this, static_cast<int>(m_items.size()) - 2));
    layout();
}
Item* SplitView::itemAt(int index) const {
    return index >= 0 && index < count() ? m_items[static_cast<std::size_t>(index)] : nullptr;
}
void SplitView::setSizes(const std::vector<float>& sizes) {
    auto sceneLock = lockScene();
    m_sizes = sizes;
    m_sizes.resize(m_items.size(), 160);
    layout();
}
void SplitView::layout() {
    auto sceneLock = lockScene();
    if (m_items.empty()) return;
    const float available = std::max(1.0f,
        (m_orientation == Orientation::Horizontal ? width() : height()) -
        m_handleSize * m_handles.size());
    float total = 0;
    for (float size : m_sizes) total += std::max(20.0f, size);
    float position = 0;
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const float size = available * std::max(20.0f, m_sizes[i]) / total;
        if (m_orientation == Orientation::Horizontal)
            m_items[i]->setGeometry(position, 0, size, height());
        else
            m_items[i]->setGeometry(0, position, width(), size);
        position += size;
        if (i < m_handles.size()) {
            if (m_orientation == Orientation::Horizontal)
                m_handles[i]->setGeometry(position, 0, m_handleSize, height());
            else
                m_handles[i]->setGeometry(0, position, width(), m_handleSize);
            position += m_handleSize;
        }
    }
}
void SplitView::moveHandle(int index, float delta) {
    auto sceneLock = lockScene();
    if (index < 0 || static_cast<std::size_t>(index + 1) >= m_sizes.size()) return;
    const auto i = static_cast<std::size_t>(index);
    const float old = m_sizes[i];
    m_sizes[i] = std::max(30.0f, m_sizes[i] + delta);
    m_sizes[i + 1] = std::max(30.0f, m_sizes[i + 1] - (m_sizes[i] - old));
    layout();
}

StackView::StackView(Object* parent) : Item(parent) {
    setClip(true);
    registerPropertyImpl("depth", typeid(int),
        [](const Object* o) -> std::any { return static_cast<const StackView*>(o)->depth(); },
        [](Object*, const std::any&) { throw std::invalid_argument("depth is read-only"); });
}
StackView::~StackView() = default;
void StackView::push(Item* item) {
    auto sceneLock = lockScene();
    if (!item) return;
    if (currentItem()) currentItem()->setVisible(false);
    item->setParent(this);
    item->fill(this);
    item->setVisible(true);
    m_stack.push_back(item);
    notifyPropertyChanged("depth");
    notifyPropertyChanged("currentItem");
}
Item* StackView::pop() {
    auto sceneLock = lockScene();
    if (m_stack.empty()) return nullptr;
    Item* removed = m_stack.back();
    m_stack.pop_back();
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
    for (Item* item : m_stack) item->setVisible(false);
    m_stack.clear();
    notifyPropertyChanged("depth");
    notifyPropertyChanged("currentItem");
}

SwipeView::SwipeView(Object* parent) : Item(parent) {
    setClip(true);
    registerProperty<int>("currentIndex", &SwipeView::currentIndex, &SwipeView::setCurrentIndex);
    registerProperty<bool>("interactive", &SwipeView::interactive, &SwipeView::setInteractive);
    registerPropertyImpl("count", typeid(int),
        [](const Object* o) -> std::any { return static_cast<const SwipeView*>(o)->count(); },
        [](Object*, const std::any&) { throw std::invalid_argument("count is read-only"); });
}
SwipeView::~SwipeView() = default;
void SwipeView::addItem(Item* item) {
    auto sceneLock = lockScene();
    if (!item) return;
    item->setParent(this);
    item->fill(this);
    m_pages.push_back(item);
    if (m_currentIndex < 0) setCurrentIndex(0);
    else item->setVisible(false);
    notifyPropertyChanged("count");
}
Item* SwipeView::itemAt(int index) const {
    return index >= 0 && index < count() ? m_pages[static_cast<std::size_t>(index)] : nullptr;
}
Item* SwipeView::currentItem() const { return itemAt(m_currentIndex); }
void SwipeView::setCurrentIndex(int index) {
    auto sceneLock = lockScene();
    if (m_pages.empty() || index < 0 || index >= count() || index == m_currentIndex) return;
    m_currentIndex = index;
    for (int i = 0; i < count(); ++i) m_pages[static_cast<std::size_t>(i)]->setVisible(i == index);
    update();
    notifyPropertyChanged("currentIndex");
}
void SwipeView::mouseEvent(MouseEvent& event) {
    if (!m_interactive) return;
    if (event.action == MouseEvent::Action::Press && event.button == 1) {
        m_pressed = true; m_pressPosition = event.pos; event.accepted = true;
    } else if (event.action == MouseEvent::Action::Release && m_pressed) {
        m_pressed = false;
        const float delta = m_orientation == Orientation::Horizontal
            ? event.pos.x - m_pressPosition.x : event.pos.y - m_pressPosition.y;
        if (delta < -30) setCurrentIndex(std::min(count() - 1, m_currentIndex + 1));
        else if (delta > 30) setCurrentIndex(std::max(0, m_currentIndex - 1));
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

TabBar::TabBar(Object* parent) : Item(parent) {
    setImplicitHeight(40);
    registerProperty<int>("currentIndex", &TabBar::currentIndex, &TabBar::setCurrentIndex);
    registerPropertyImpl("count", typeid(int),
        [](const Object* o) -> std::any { return static_cast<const TabBar*>(o)->count(); },
        [](Object*, const std::any&) { throw std::invalid_argument("count is read-only"); });
}
TabBar::~TabBar() = default;
void TabBar::addTab(String text) {
    auto sceneLock = lockScene();
    auto* button = new Button(this);
    button->setCheckable(true);
    button->setText(std::move(text));
    button->connect<>(button, AbstractButton::signalClicked, [this, button] {
        const auto found = std::find(m_tabs.begin(), m_tabs.end(), button);
        if (found != m_tabs.end()) setCurrentIndex(static_cast<int>(found - m_tabs.begin()));
    }, ConnectionMode::Direct);
    m_tabs.push_back(button);
    if (m_currentIndex < 0) setCurrentIndex(0);
    layout();
    notifyPropertyChanged("count");
}
String TabBar::textAt(int index) const {
    auto* button = tabAt(index);
    return button ? button->text() : String();
}
Button* TabBar::tabAt(int index) const {
    return index >= 0 && index < count() ? m_tabs[static_cast<std::size_t>(index)] : nullptr;
}
void TabBar::setCurrentIndex(int index) {
    auto sceneLock = lockScene();
    if (index < 0 || index >= count()) return;
    m_currentIndex = index;
    for (int i = 0; i < count(); ++i) m_tabs[static_cast<std::size_t>(i)]->setChecked(i == index);
    notifyPropertyChanged("currentIndex");
    emitSignal(signalCurrentIndexChanged, index);
}
void TabBar::layout() {
    auto sceneLock = lockScene();
    if (m_tabs.empty()) return;
    const float tabWidth = (width() > 0 ? width() : 120.0f * m_tabs.size()) / m_tabs.size();
    for (std::size_t i = 0; i < m_tabs.size(); ++i)
        m_tabs[i]->setGeometry(i * tabWidth, 0, tabWidth, height() > 0 ? height() : 40);
}
} // namespace Controls
} // namespace Gui
} // namespace Firmir
