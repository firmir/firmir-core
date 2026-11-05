#pragma once

#include <firmir/gui/events.hpp>
#include <firmir/object.hpp>

#include <memory>
#include <mutex>
#include <vector>

namespace Firmir {
namespace Gui {

class Animation;
class Item;
class Painter;
class Window;

/// Point around which rotation and scale are applied (QML transformOrigin).
enum class TransformOrigin {
    TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight
};

/// Keyboard focus acquisition policy (Qt::FocusPolicy-compatible values).
enum class FocusPolicy {
    NoFocus = 0,
    TabFocus = 0x1,
    ClickFocus = 0x2,
    StrongFocus = 0x3,
    WheelFocus = 0x4,
};

/// Anchor lines of an item (QML anchor lines analogue).
enum class AnchorLine {
    Left,
    Right,
    HorizontalCenter,
    Top,
    Bottom,
    VerticalCenter
};

/**
 * @brief Target of an anchor: an item plus one of its lines.
 *
 * The item must be the parent item or a sibling (same parent); an empty
 * target (item == nullptr) clears the anchor. Geometry of the target is
 * tracked: the anchored item re-computes whenever the target's x/y/width/
 * height changes.
 */
struct AnchorTarget {
    Item* item = nullptr;
    AnchorLine line = AnchorLine::Left;
};

/**
 * @brief 2D scene-graph item (QQuickItem analogue).
 *
 * Items form a tree: the Object parent of an Item is its parent item (or the
 * Window for the root item). Each item has geometry in parent coordinates
 * plus opacity/visible/enabled, paints its own content in paint(), and can
 * receive mouse/touch/key input. The tree may be freely mutated from the GUI
 * thread (re-parenting, property changes); rendering happens on the owning
 * window's render thread, synchronized internally.
 */
class Item : public Object {
public:
    /// The parent is either another Item or a Window (for the root item).
    explicit Item(Object* parent = nullptr);
    ~Item() override;

    float x() const;
    void setX(float x);
    float y() const;
    void setY(float y);
    float width() const;
    void setWidth(float width);
    float height() const;
    void setHeight(float height);
    /// QML implicit size: used for a dimension without an explicit write.
    float implicitWidth() const;
    void setImplicitWidth(float width);
    float implicitHeight() const;
    void setImplicitHeight(float height);
    /// Read-only union of the direct children's geometry in local coordinates.
    RectF childrenRect() const;
    void setGeometry(float x, float y, float width, float height);
    /// Geometry in parent-item coordinates.
    RectF geometry() const;

    /// Own opacity, 0 (transparent) .. 1 (opaque); default 1.
    float opacity() const;
    void setOpacity(float opacity);
    /// Opacity multiplied with all ancestors' opacity.
    float effectiveOpacity() const;

    bool isVisible() const;
    void setVisible(bool visible);
    bool isEnabled() const;
    void setEnabled(bool enabled);

    /// Clockwise rotation in degrees and uniform scale around transformOrigin.
    float rotation() const;
    void setRotation(float degrees);
    float scale() const;
    void setScale(float scale);
    TransformOrigin transformOrigin() const;
    void setTransformOrigin(TransformOrigin origin);
    /// Clip this item's own content and descendants to its bounds (default false).
    bool clip() const;
    void setClip(bool clip);
    /// Stacking order among siblings; equal z preserves declaration order.
    float z() const;
    void setZ(float z);

    /// Keyboard focus within the owning window.
    bool hasFocus() const;
    void setFocus(bool focus = true);
    /// True when this item owns the visible window's key focus (read-only).
    bool hasActiveFocus() const;
    bool activeFocusOnTab() const;
    void setActiveFocusOnTab(bool enabled);
    FocusPolicy focusPolicy() const;
    void setFocusPolicy(FocusPolicy policy);

    /**
     * @brief Anchors (QML anchors analogue).
     *
     * A line anchor pins one of this item's lines to a line of the parent or
     * a sibling; the item re-layouts when the target geometry, the margins or
     * its own size change. Setting both left and right stretches the width
     * (same for top/bottom and height); horizontalCenter applies only when
     * neither left nor right is set (same for verticalCenter). Margins are
     * applied inside the anchored edge; center offsets shift the center.
     */
    void setAnchorLeft(AnchorTarget target);
    void setAnchorRight(AnchorTarget target);
    void setAnchorHorizontalCenter(AnchorTarget target);
    void setAnchorTop(AnchorTarget target);
    void setAnchorBottom(AnchorTarget target);
    void setAnchorVerticalCenter(AnchorTarget target);
    /// Anchors all four sides to the target's edges; nullptr clears them.
    void fill(Item* target);
    /// Anchors both centers to the target's centers; nullptr clears them.
    void centerIn(Item* target);
    /// Clears all six line anchors.
    void clearAnchors();

    AnchorTarget anchorLeft() const;
    AnchorTarget anchorRight() const;
    AnchorTarget anchorHorizontalCenter() const;
    AnchorTarget anchorTop() const;
    AnchorTarget anchorBottom() const;
    AnchorTarget anchorVerticalCenter() const;

    float leftMargin() const;
    void setLeftMargin(float margin);
    float rightMargin() const;
    void setRightMargin(float margin);
    float topMargin() const;
    void setTopMargin(float margin);
    float bottomMargin() const;
    void setBottomMargin(float margin);
    /// Sets all four margins at once.
    void setMargins(float margin);
    float horizontalCenterOffset() const;
    void setHorizontalCenterOffset(float offset);
    float verticalCenterOffset() const;
    void setVerticalCenterOffset(float offset);

    /**
     * @brief Attaches a behavior to a property (QML `Behavior on` analogue):
     * subsequent setProperty() writes to the property are consumed and the
     * animation (tree) is restarted towards the new value instead.
     *
     * The animation's leaf targets/properties/`to` are injected on every
     * trigger; `from` is the value at trigger time. Re-adding a behavior for
     * the same property replaces the animation. The animation is not adopted;
     * parent it to the item.
     */
    void addBehavior(const std::string& property, Animation* animation);
    /// Returns the behavior animation for the property, or nullptr.
    Animation* behavior(const std::string& property) const;

    Item* parentItem() const;
    std::vector<Item*> childItems() const;
    /// Topmost visible child (deep) containing the point (this item's
    /// coordinates), or nullptr. Disabled items are skipped.
    Item* itemAt(float x, float y) const;
    /// Maps a point from item coordinates to window (scene) coordinates.
    Point mapToScene(Point p) const;
    /// Maps a point from window (scene) coordinates to item coordinates.
    Point mapFromScene(Point p) const;
    /// Maps a point between this item's and its parent's coordinates.
    Point mapToParent(Point p) const;
    Point mapFromParent(Point p) const;
    /// transformOrigin as an item-local point.
    Point transformOriginPoint() const;

    /// The window this item belongs to, or nullptr when not attached.
    Window* window() const;

    /**
     * @brief Creates a child item of type T under this item.
     *
     * When the item is attached to a window, the construction happens under
     * the window's scene mutex (safe against the render thread) — prefer it
     * over a bare `new T(this)`.
     */
    template <typename T = Item, typename... Args>
    T* createChild(Args&&... args);

    /// Schedules a repaint of the owning window. Thread-safe.
    void update();

protected:
    /// Serializes subclass-owned state with the owning window's render thread.
    std::unique_lock<std::recursive_mutex> lockScene() const;
    /// Paints the item content. Called on the render thread; reimplement and
    /// use the painter (coordinates are item-local).
    virtual void paint(Painter& painter);
    /// Input handlers; set @p accepted on the event to stop propagation to
    /// parent items. Called on the GUI thread.
    virtual void mouseEvent(MouseEvent& event);
    virtual void touchEvent(TouchEvent& event);
    virtual void keyEvent(KeyEvent& event);
    virtual void focusInEvent();
    virtual void focusOutEvent();

private:
    /// Paints this item and its children. Called by Window on the render
    /// thread with the scene mutex already held.
    void render(Painter& painter);

    /// Recomputes the geometry from the anchors (no-op when none are set).
    void recomputeAnchors();
    /// Assigns one anchor slot (0 = left, 1 = right, 2 = horizontalCenter,
    /// 3 = top, 4 = bottom, 5 = verticalCenter) and recomputes the geometry.
    void assignAnchor(int slotIndex, AnchorTarget target);
    /// The property write hook: runs the behavior animation, returns true
    /// when the write was consumed.
    bool behaviorWrite(const std::string& name, const std::any& value);

    class Impl;
    std::unique_ptr<Impl> d;

    friend class Window;
};

} // namespace Gui
} // namespace Firmir

#include <firmir/gui/window.hpp>

namespace Firmir {
namespace Gui {

template <typename T, typename... Args>
T* Item::createChild(Args&&... args) {
    if (Window* w = window())
        return w->createItem<T>(this, std::forward<Args>(args)...);
    return new T(this, std::forward<Args>(args)...);
}

} // namespace Gui
} // namespace Firmir
