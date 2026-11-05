#include <firmir/gui/item.hpp>

#include "detail/window_internal.hpp"

#include <algorithm>
#include <cmath>
#include <firmir/gui/animation.hpp>
#include <firmir/gui/painter.hpp>
#include <firmir/gui/window.hpp>

namespace Firmir {
namespace Gui {

class Item::Impl {
public:
    /// One line anchor: the target plus the subscription to its changes.
    struct AnchorSlot {
        AnchorTarget target; // target.item == nullptr: not set
        Connection connection;
        Connection destroyedConnection;
    };

    float x = 0;
    float y = 0;
    float width = 0;  // effective width (explicit or implicit)
    float height = 0; // effective height (explicit or implicit)
    float implicitWidth = 0;
    float implicitHeight = 0;
    bool explicitWidth = false;
    bool explicitHeight = false;
    float opacity = 1;
    bool visible = true;
    bool enabled = true;
    float rotation = 0;
    float scale = 1;
    TransformOrigin transformOrigin = TransformOrigin::Center;
    bool clip = false;
    float z = 0;
    bool activeFocusOnTab = false;
    // Preserve the existing click-to-focus behavior by default.
    FocusPolicy focusPolicy = FocusPolicy::StrongFocus;

    AnchorSlot anchorLeft;
    AnchorSlot anchorRight;
    AnchorSlot anchorHorizontalCenter;
    AnchorSlot anchorTop;
    AnchorSlot anchorBottom;
    AnchorSlot anchorVerticalCenter;
    float leftMargin = 0;
    float rightMargin = 0;
    float topMargin = 0;
    float bottomMargin = 0;
    float horizontalCenterOffset = 0;
    float verticalCenterOffset = 0;
    Connection selfGeometryConnection;
    bool recomputingAnchors = false;

    /// Behaviors: property name -> animation tree (at most one per property).
    std::vector<std::pair<std::string, Animation*>> behaviors;
};

namespace {

/// True when the property participates in the anchor dependency set.
bool isGeometryProperty(const std::string& name) {
    return name == "x" || name == "y" || name == "width" || name == "height";
}

/// The target's rectangle in our parent item's coordinates: the parent item
/// is addressed by its bounds (0,0,w,h), a sibling by its own geometry.
RectF anchorTargetRect(const Item* self, const Item* target) {
    if (target == self->parentItem())
        return RectF{0, 0, target->width(), target->height()};
    return target->geometry();
}

float anchorLineValue(const RectF& rect, AnchorLine line) {
    switch (line) {
    case AnchorLine::Left: return rect.x;
    case AnchorLine::Right: return rect.x + rect.width;
    case AnchorLine::HorizontalCenter: return rect.x + rect.width / 2;
    case AnchorLine::Top: return rect.y;
    case AnchorLine::Bottom: return rect.y + rect.height;
    case AnchorLine::VerticalCenter: return rect.y + rect.height / 2;
    }
    return 0;
}

} // namespace

Item::Item(Object* parent) : Object(nullptr), d(std::make_unique<Impl>()) {
    registerProperty<float>("x", &Item::x, &Item::setX);
    registerProperty<float>("y", &Item::y, &Item::setY);
    registerProperty<float>("width", &Item::width, &Item::setWidth);
    registerProperty<float>("height", &Item::height, &Item::setHeight);
    registerProperty<float>("opacity", &Item::opacity, &Item::setOpacity);
    registerProperty<bool>("visible", &Item::isVisible, &Item::setVisible);
    registerProperty<bool>("enabled", &Item::isEnabled, &Item::setEnabled);
    registerProperty<float>("implicitWidth", &Item::implicitWidth,
                            &Item::setImplicitWidth);
    registerProperty<float>("implicitHeight", &Item::implicitHeight,
                            &Item::setImplicitHeight);
    registerProperty<float>("rotation", &Item::rotation, &Item::setRotation);
    registerProperty<float>("scale", &Item::scale, &Item::setScale);
    registerProperty<bool>("clip", &Item::clip, &Item::setClip);
    registerProperty<float>("z", &Item::z, &Item::setZ);
    registerProperty<bool>("focus", &Item::hasFocus, &Item::setFocus);
    registerProperty<bool>("activeFocusOnTab", &Item::activeFocusOnTab,
                            &Item::setActiveFocusOnTab);
    registerPropertyImpl(
        "childrenRect", typeid(RectF),
        [](const Object* object) -> std::any {
            return static_cast<const Item*>(object)->childrenRect();
        },
        [](Object*, const std::any&) {
            throw std::invalid_argument("childrenRect is read-only");
        });
    registerPropertyImpl(
        "activeFocus", typeid(bool),
        [](const Object* object) -> std::any {
            return static_cast<const Item*>(object)->hasActiveFocus();
        },
        [](Object*, const std::any&) {
            throw std::invalid_argument("activeFocus is read-only");
        });
    registerPropertyImpl(
        "transformOrigin", typeid(TransformOrigin),
        [](const Object* object) -> std::any {
            return static_cast<const Item*>(object)->transformOrigin();
        },
        [](Object* object, const std::any& value) {
            static_cast<Item*>(object)->setTransformOrigin(
                static_cast<TransformOrigin>(Firmir::detail::anyConvert<int>(value)));
        });
    registerPropertyImpl(
        "focusPolicy", typeid(FocusPolicy),
        [](const Object* object) -> std::any {
            return static_cast<const Item*>(object)->focusPolicy();
        },
        [](Object* object, const std::any& value) {
            static_cast<Item*>(object)->setFocusPolicy(
                static_cast<FocusPolicy>(Firmir::detail::anyConvert<int>(value)));
        });

    // Own size changes re-resolve the right/bottom/center anchors.
    d->selfGeometryConnection =
        connect<std::string, std::any>(this, signalPropertyChanged,
                                       [this](const std::string& name, const std::any&) {
                                           if (name == "width" || name == "height")
                                               recomputeAnchors();
                                       },
                                       ConnectionMode::Direct);

    // Behaviors intercept property writes (QML Behavior on ... semantics).
    setPropertyWriteHook(
        [this](Object&, const std::string& name, const std::any& value) {
            return behaviorWrite(name, value);
        });

    // Attach only after d is initialized: the render thread may pick up the
    // item as soon as it appears in the parent's children list (the core
    // children mutex provides the happens-before), and the fence below
    // orders the initialization against the first render.
    connect(this, "childrenChanged",
            [this] { notifyPropertyChanged("childrenRect"); },
            ConnectionMode::Direct);

    if (parent) {
        setParent(parent);
        if (Window* w = window()) {
            detail::sceneMutex(w).lock();
            detail::sceneMutex(w).unlock();
            detail::scheduleRender(w);
        }
    }
}

Item::~Item() {
    clearAnchors();
    d->selfGeometryConnection.disconnect();
    // Detach from the scene under the scene mutex so the render thread can
    // never observe a half-destroyed item.
    if (Window* w = window()) {
        std::lock_guard<std::recursive_mutex> lock(detail::sceneMutex(w));
        detail::clearFocusItem(w, this);
        setParent(nullptr);
    }
}

float Item::x() const { return d->x; }
float Item::y() const { return d->y; }
float Item::width() const { return d->width; }
float Item::height() const { return d->height; }
float Item::opacity() const { return d->opacity; }
bool Item::isVisible() const { return d->visible; }
bool Item::isEnabled() const { return d->enabled; }

void Item::setX(float x) { setGeometry(x, d->y, d->width, d->height); }
void Item::setY(float y) { setGeometry(d->x, y, d->width, d->height); }
void Item::setWidth(float w) {
    d->explicitWidth = true;
    setGeometry(d->x, d->y, w, d->height);
}
void Item::setHeight(float h) {
    d->explicitHeight = true;
    setGeometry(d->x, d->y, d->width, h);
}

float Item::implicitWidth() const { return d->implicitWidth; }
float Item::implicitHeight() const { return d->implicitHeight; }

void Item::setImplicitWidth(float width) {
    if (d->implicitWidth == width)
        return;
    d->implicitWidth = width;
    if (!d->explicitWidth)
        setGeometry(d->x, d->y, width, d->height);
    notifyPropertyChanged("implicitWidth");
}

void Item::setImplicitHeight(float height) {
    if (d->implicitHeight == height)
        return;
    d->implicitHeight = height;
    if (!d->explicitHeight)
        setGeometry(d->x, d->y, d->width, height);
    notifyPropertyChanged("implicitHeight");
}

RectF Item::childrenRect() const {
    bool first = true;
    RectF result{};
    for (Item* child : childItems()) {
        const RectF r = child->geometry();
        if (first) {
            result = r;
            first = false;
            continue;
        }
        const float x2 = std::max(result.x + result.width, r.x + r.width);
        const float y2 = std::max(result.y + result.height, r.y + r.height);
        result.x = std::min(result.x, r.x);
        result.y = std::min(result.y, r.y);
        result.width = x2 - result.x;
        result.height = y2 - result.y;
    }
    return result;
}

void Item::setGeometry(float x, float y, float width, float height) {
    const bool changedX = d->x != x;
    const bool changedY = d->y != y;
    const bool changedW = d->width != width;
    const bool changedH = d->height != height;
    if (!changedX && !changedY && !changedW && !changedH)
        return;
    Window* w = window();
    if (w)
        detail::sceneMutex(w).lock();
    d->x = x;
    d->y = y;
    d->width = width;
    d->height = height;
    if (w) {
        detail::sceneMutex(w).unlock();
        update();
    }
    if (changedX)
        notifyPropertyChanged("x");
    if (changedY)
        notifyPropertyChanged("y");
    if (changedW)
        notifyPropertyChanged("width");
    if (changedH)
        notifyPropertyChanged("height");
}

RectF Item::geometry() const { return {d->x, d->y, d->width, d->height}; }

void Item::setOpacity(float opacity) {
    const float clamped = std::clamp(opacity, 0.0f, 1.0f);
    if (d->opacity == clamped)
        return;
    Window* w = window();
    if (w)
        detail::sceneMutex(w).lock();
    d->opacity = clamped;
    if (w) {
        detail::sceneMutex(w).unlock();
        update();
    }
    notifyPropertyChanged("opacity");
}

float Item::effectiveOpacity() const {
    float result = d->opacity;
    for (Item* p = parentItem(); p; p = p->parentItem())
        result *= p->opacity();
    return result;
}

void Item::setVisible(bool visible) {
    if (d->visible == visible)
        return;
    Window* w = window();
    if (w)
        detail::sceneMutex(w).lock();
    d->visible = visible;
    if (w) {
        detail::sceneMutex(w).unlock();
        update();
    }
    notifyPropertyChanged("visible");
}

void Item::setEnabled(bool enabled) {
    if (d->enabled == enabled)
        return;
    Window* w = window();
    if (w)
        detail::sceneMutex(w).lock();
    d->enabled = enabled;
    if (w)
        detail::sceneMutex(w).unlock();
    notifyPropertyChanged("enabled");
}

float Item::rotation() const { return d->rotation; }
float Item::scale() const { return d->scale; }
TransformOrigin Item::transformOrigin() const { return d->transformOrigin; }
bool Item::clip() const { return d->clip; }
float Item::z() const { return d->z; }

void Item::setRotation(float degrees) {
    if (d->rotation == degrees)
        return;
    d->rotation = degrees;
    update();
    notifyPropertyChanged("rotation");
}

void Item::setScale(float scale) {
    if (d->scale == scale)
        return;
    d->scale = scale;
    update();
    notifyPropertyChanged("scale");
}

void Item::setTransformOrigin(TransformOrigin origin) {
    if (d->transformOrigin == origin)
        return;
    d->transformOrigin = origin;
    update();
    notifyPropertyChanged("transformOrigin");
}

void Item::setClip(bool clip) {
    if (d->clip == clip)
        return;
    d->clip = clip;
    update();
    notifyPropertyChanged("clip");
}

void Item::setZ(float z) {
    if (d->z == z)
        return;
    d->z = z;
    update();
    notifyPropertyChanged("z");
}

bool Item::activeFocusOnTab() const { return d->activeFocusOnTab; }
void Item::setActiveFocusOnTab(bool enabled) {
    if (d->activeFocusOnTab == enabled)
        return;
    d->activeFocusOnTab = enabled;
    notifyPropertyChanged("activeFocusOnTab");
}

FocusPolicy Item::focusPolicy() const { return d->focusPolicy; }
void Item::setFocusPolicy(FocusPolicy policy) {
    if (d->focusPolicy == policy)
        return;
    d->focusPolicy = policy;
    if (policy == FocusPolicy::NoFocus)
        setFocus(false);
    notifyPropertyChanged("focusPolicy");
}

bool Item::hasFocus() const {
    Window* w = window();
    return w && w->focusItem() == this;
}

void Item::setFocus(bool focus) {
    if (Window* w = window())
        w->setFocusItem(focus ? this : nullptr);
}

bool Item::hasActiveFocus() const {
    Window* w = window();
    return w && w->isVisible() && w->focusItem() == this;
}

void Item::assignAnchor(int slotIndex, AnchorTarget target) {
    Impl::AnchorSlot& slot = [&]() -> Impl::AnchorSlot& {
        switch (slotIndex) {
        case 0: return d->anchorLeft;
        case 1: return d->anchorRight;
        case 2: return d->anchorHorizontalCenter;
        case 3: return d->anchorTop;
        case 4: return d->anchorBottom;
        default: return d->anchorVerticalCenter;
        }
    }();
    slot.connection.disconnect();
    slot.destroyedConnection.disconnect();
    slot.target = target;
    if (target.item) {
        slot.connection = target.item->connect<std::string, std::any>(
            this, signalPropertyChanged,
            [this](const std::string& name, const std::any&) {
                if (isGeometryProperty(name))
                    recomputeAnchors();
            },
            ConnectionMode::Direct);
        // The target may be destroyed before us; drop the anchor then.
        slot.destroyedConnection = target.item->connect(
            this, signalDestroyed,
            [this, &slot] {
                slot.connection.disconnect();
                slot.destroyedConnection.disconnect();
                slot.target = AnchorTarget{};
            },
            ConnectionMode::Direct);
    }
    recomputeAnchors();
}

void Item::setAnchorLeft(AnchorTarget target) { assignAnchor(0, std::move(target)); }
void Item::setAnchorRight(AnchorTarget target) { assignAnchor(1, std::move(target)); }
void Item::setAnchorHorizontalCenter(AnchorTarget target) {
    assignAnchor(2, std::move(target));
}
void Item::setAnchorTop(AnchorTarget target) { assignAnchor(3, std::move(target)); }
void Item::setAnchorBottom(AnchorTarget target) { assignAnchor(4, std::move(target)); }
void Item::setAnchorVerticalCenter(AnchorTarget target) {
    assignAnchor(5, std::move(target));
}

void Item::fill(Item* target) {
    setAnchorLeft(AnchorTarget{target, AnchorLine::Left});
    setAnchorRight(AnchorTarget{target, AnchorLine::Right});
    setAnchorTop(AnchorTarget{target, AnchorLine::Top});
    setAnchorBottom(AnchorTarget{target, AnchorLine::Bottom});
}

void Item::centerIn(Item* target) {
    setAnchorHorizontalCenter(AnchorTarget{target, AnchorLine::HorizontalCenter});
    setAnchorVerticalCenter(AnchorTarget{target, AnchorLine::VerticalCenter});
}

void Item::clearAnchors() {
    for (int i = 0; i < 6; ++i)
        assignAnchor(i, AnchorTarget{});
}

AnchorTarget Item::anchorLeft() const { return d->anchorLeft.target; }
AnchorTarget Item::anchorRight() const { return d->anchorRight.target; }
AnchorTarget Item::anchorHorizontalCenter() const {
    return d->anchorHorizontalCenter.target;
}
AnchorTarget Item::anchorTop() const { return d->anchorTop.target; }
AnchorTarget Item::anchorBottom() const { return d->anchorBottom.target; }
AnchorTarget Item::anchorVerticalCenter() const {
    return d->anchorVerticalCenter.target;
}

float Item::leftMargin() const { return d->leftMargin; }
float Item::rightMargin() const { return d->rightMargin; }
float Item::topMargin() const { return d->topMargin; }
float Item::bottomMargin() const { return d->bottomMargin; }
float Item::horizontalCenterOffset() const { return d->horizontalCenterOffset; }
float Item::verticalCenterOffset() const { return d->verticalCenterOffset; }

void Item::setLeftMargin(float margin) {
    d->leftMargin = margin;
    recomputeAnchors();
}
void Item::setRightMargin(float margin) {
    d->rightMargin = margin;
    recomputeAnchors();
}
void Item::setTopMargin(float margin) {
    d->topMargin = margin;
    recomputeAnchors();
}
void Item::setBottomMargin(float margin) {
    d->bottomMargin = margin;
    recomputeAnchors();
}
void Item::setMargins(float margin) {
    d->leftMargin = d->rightMargin = d->topMargin = d->bottomMargin = margin;
    recomputeAnchors();
}
void Item::setHorizontalCenterOffset(float offset) {
    d->horizontalCenterOffset = offset;
    recomputeAnchors();
}
void Item::setVerticalCenterOffset(float offset) {
    d->verticalCenterOffset = offset;
    recomputeAnchors();
}

void Item::recomputeAnchors() {
    if (d->recomputingAnchors)
        return;
    d->recomputingAnchors = true;
    const auto lineValue = [this](const Impl::AnchorSlot& slot) {
        return anchorLineValue(anchorTargetRect(this, slot.target.item),
                               slot.target.line);
    };
    float newX = d->x;
    float newY = d->y;
    float newWidth = d->width;
    float newHeight = d->height;

    const bool hasLeft = d->anchorLeft.target.item != nullptr;
    const bool hasRight = d->anchorRight.target.item != nullptr;
    const bool hasHCenter = d->anchorHorizontalCenter.target.item != nullptr;
    if (hasLeft && hasRight) {
        newX = lineValue(d->anchorLeft) + d->leftMargin;
        newWidth = lineValue(d->anchorRight) - d->rightMargin - newX;
    } else if (hasLeft) {
        newX = lineValue(d->anchorLeft) + d->leftMargin;
    } else if (hasRight) {
        newX = lineValue(d->anchorRight) - d->rightMargin - newWidth;
    } else if (hasHCenter) {
        newX = lineValue(d->anchorHorizontalCenter) - newWidth / 2 +
               d->horizontalCenterOffset;
    }

    const bool hasTop = d->anchorTop.target.item != nullptr;
    const bool hasBottom = d->anchorBottom.target.item != nullptr;
    const bool hasVCenter = d->anchorVerticalCenter.target.item != nullptr;
    if (hasTop && hasBottom) {
        newY = lineValue(d->anchorTop) + d->topMargin;
        newHeight = lineValue(d->anchorBottom) - d->bottomMargin - newY;
    } else if (hasTop) {
        newY = lineValue(d->anchorTop) + d->topMargin;
    } else if (hasBottom) {
        newY = lineValue(d->anchorBottom) - d->bottomMargin - newHeight;
    } else if (hasVCenter) {
        newY = lineValue(d->anchorVerticalCenter) - newHeight / 2 +
               d->verticalCenterOffset;
    }

    setGeometry(newX, newY, newWidth, newHeight);
    d->recomputingAnchors = false;
}

void Item::addBehavior(const std::string& property, Animation* animation) {
    for (auto& entry : d->behaviors) {
        if (entry.first == property) {
            entry.second = animation;
            return;
        }
    }
    d->behaviors.emplace_back(property, animation);
}

Animation* Item::behavior(const std::string& property) const {
    for (const auto& entry : d->behaviors)
        if (entry.first == property)
            return entry.second;
    return nullptr;
}

bool Item::behaviorWrite(const std::string& name, const std::any& value) {
    Animation* animation = behavior(name);
    if (!animation)
        return false;
    detail::prepareBehaviorAnimation(animation, this, name, value);
    if (animation->running())
        animation->stop();
    animation->start();
    return true;
}

Item* Item::parentItem() const { return dynamic_cast<Item*>(parent()); }

std::vector<Item*> Item::childItems() const {
    std::vector<Item*> result;
    for (Object* child : children())
        if (Item* item = dynamic_cast<Item*>(child))
            result.push_back(item);
    std::stable_sort(result.begin(), result.end(),
                     [](const Item* a, const Item* b) { return a->z() < b->z(); });
    return result;
}

Item* Item::itemAt(float x, float y) const {
    if (!d->visible || !d->enabled)
        return nullptr;
    const auto kids = childItems();
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        Item* child = *it;
        const Point local = child->mapFromParent({x, y});
        if (Item* hit = child->itemAt(local.x, local.y))
            return hit;
    }
    return RectF{0, 0, d->width, d->height}.contains(x, y)
               ? const_cast<Item*>(this)
               : nullptr;
}

Point Item::transformOriginPoint() const {
    const int index = static_cast<int>(d->transformOrigin);
    const float fx = static_cast<float>(index % 3) * 0.5f;
    const float fy = static_cast<float>(index / 3) * 0.5f;
    return {d->width * fx, d->height * fy};
}

Point Item::mapToParent(Point p) const {
    const Point origin = transformOriginPoint();
    p.x -= origin.x;
    p.y -= origin.y;
    if (d->rotation != 0 || d->scale != 1) {
        constexpr float pi = 3.14159265358979323846f;
        const float angle = d->rotation * pi / 180.0f;
        const float cs = std::cos(angle);
        const float sn = std::sin(angle);
        const float x = p.x;
        const float y = p.y;
        p.x = d->scale * (cs * x - sn * y);
        p.y = d->scale * (sn * x + cs * y);
    }
    return {p.x + origin.x + d->x, p.y + origin.y + d->y};
}

Point Item::mapFromParent(Point p) const {
    const Point origin = transformOriginPoint();
    p.x -= d->x + origin.x;
    p.y -= d->y + origin.y;
    if (d->scale == 0)
        return {};
    const float angle = -d->rotation * 3.14159265358979323846f / 180.0f;
    const float cs = std::cos(angle);
    const float sn = std::sin(angle);
    const float x = p.x;
    const float y = p.y;
    p.x = (cs * x - sn * y) / d->scale;
    p.y = (sn * x + cs * y) / d->scale;
    return {p.x + origin.x, p.y + origin.y};
}

Point Item::mapToScene(Point p) const {
    for (const Item* item = this; item; item = item->parentItem())
        p = item->mapToParent(p);
    return p;
}

Point Item::mapFromScene(Point p) const {
    std::vector<const Item*> chain;
    for (const Item* item = this; item; item = item->parentItem())
        chain.push_back(item);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
        p = (*it)->mapFromParent(p);
    return p;
}

Window* Item::window() const {
    for (Object* p = parent(); p; p = p->parent())
        if (Window* w = dynamic_cast<Window*>(p))
            return w;
    return nullptr;
}

std::unique_lock<std::recursive_mutex> Item::lockScene() const {
    if (Window* w = window())
        return std::unique_lock<std::recursive_mutex>(detail::sceneMutex(w));
    return std::unique_lock<std::recursive_mutex>();
}

void Item::update() {
    if (Window* w = window())
        detail::scheduleRender(w);
}

void Item::paint(Painter&) {}
void Item::mouseEvent(MouseEvent&) {}
void Item::touchEvent(TouchEvent&) {}
void Item::keyEvent(KeyEvent&) {}
void Item::focusInEvent() {}
void Item::focusOutEvent() {}

void Item::render(Painter& painter) {
    if (!d->visible || d->opacity <= 0)
        return;
    painter.save();
    painter.translate(d->x, d->y);
    const Point origin = transformOriginPoint();
    painter.translate(origin.x, origin.y);
    painter.rotate(d->rotation);
    painter.scale(d->scale, d->scale);
    painter.translate(-origin.x, -origin.y);
    painter.multiplyOpacity(d->opacity);
    if (d->clip) {
        painter.save();
        painter.clipRect(RectF{0, 0, d->width, d->height});
    }
    paint(painter);
    for (Item* child : childItems())
        child->render(painter);
    if (d->clip)
        painter.restore();
    painter.restore();
}

} // namespace Gui
} // namespace Firmir
