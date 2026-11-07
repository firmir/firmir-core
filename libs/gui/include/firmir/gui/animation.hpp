#pragma once

#include <firmir/gui/painter.hpp>
#include <firmir/object.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace Firmir {
namespace Gui {

class Item;

/// Easing curves supported by the animations (the firmlc whitelist).
enum class Easing { Linear, InQuad, OutQuad, InOutQuad, OutCubic, OutExpo };

/// Applies the easing curve to @p t (0..1), returning the eased progress.
double applyEasing(Easing easing, double t);

/**
 * @brief Animation base class.
 *
 * Concrete animations: PropertyAnimation (numeric), NumberAnimation,
 * ColorAnimation, PauseAnimation and the SequentialAnimation /
 * ParallelAnimation containers (children run one after another / together).
 * start() drives the animation on the owner thread's event loop; natural
 * completion emits the "finished" signal.
 */
class Animation : public Object {
public:
    explicit Animation(Object* parent = nullptr);
    ~Animation() override;

    Animation(const Animation&) = delete;
    Animation& operator=(const Animation&) = delete;

    bool running() const;
    /// Starts the animation; no-op while running.
    void start();
    /// Stops the animation without emitting "finished".
    void stop();

    /// Direct child animations (for the container animations), in creation
    /// order. Add children (parent them) before start().
    std::vector<Animation*> childAnimations() const;

    static constexpr auto signalFinished = "finished";

protected:
    /// Starts the machinery; called by start() after running=true.
    virtual void onStart();
    /// Stops the machinery; called by stop() after running=false.
    virtual void onStop();
    /// Natural completion: running=false and "finished" emitted (once).
    void finish();

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

/**
 * @brief Animates a numeric property of an Item towards a target value.
 *
 * The animation is timer-driven (~60 fps) on the owner thread's event loop
 * and writes through Object::setPropertyDirect(), so behaviors, anchors and
 * propertyChanged subscribers follow every step. When `from` is unset the
 * current property value at start() is used (Behavior semantics).
 */
class PropertyAnimation : public Animation {
public:
    explicit PropertyAnimation(Object* parent = nullptr);
    PropertyAnimation(Item* target, std::string property, Object* parent = nullptr);
    ~PropertyAnimation() override;

    Item* target() const;
    void setTargetItem(Item* target);
    std::string property() const;
    void setTargetProperty(const std::string& property);

    void setFrom(double from);
    double from() const;
    bool hasFrom() const;
    void setTo(double to);
    double to() const;
    void setDuration(std::chrono::milliseconds duration);
    std::chrono::milliseconds duration() const;
    void setEasing(Easing easing);
    Easing easing() const;

protected:
    void onStart() override;
    void onStop() override;

private:
    void tick();

    class Impl;
    std::unique_ptr<Impl> d;
};

/// The numeric PropertyAnimation.
class NumberAnimation : public PropertyAnimation {
public:
    using PropertyAnimation::PropertyAnimation;
};

/// Animates a Color property (per-channel interpolation).
class ColorAnimation : public Animation {
public:
    explicit ColorAnimation(Object* parent = nullptr);
    ~ColorAnimation() override;

    Item* target() const;
    void setTargetItem(Item* target);
    std::string property() const;
    void setTargetProperty(const std::string& property);

    void setTo(Color to);
    Color to() const;
    void setDuration(std::chrono::milliseconds duration);
    std::chrono::milliseconds duration() const;
    void setEasing(Easing easing);
    Easing easing() const;

protected:
    void onStart() override;
    void onStop() override;

private:
    void tick();

    class Impl;
    std::unique_ptr<Impl> d;
};

/// Waits for the given duration inside a SequentialAnimation.
class PauseAnimation : public Animation {
public:
    explicit PauseAnimation(Object* parent = nullptr);
    ~PauseAnimation() override;

    void setDuration(std::chrono::milliseconds duration);
    std::chrono::milliseconds duration() const;

protected:
    void onStart() override;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

/// Runs the child animations one after another (SequentialAnimation).
class SequentialAnimation : public Animation {
public:
    explicit SequentialAnimation(Object* parent = nullptr);
    ~SequentialAnimation() override;

protected:
    void onStart() override;
    void onStop() override;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

/// Runs the child animations together (ParallelAnimation); finishes when
/// the last child finishes.
class ParallelAnimation : public Animation {
public:
    explicit ParallelAnimation(Object* parent = nullptr);
    ~ParallelAnimation() override;

protected:
    void onStart() override;
    void onStop() override;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

namespace detail {

/// Injects the behavior context (target item, property, target value) into
/// every leaf animation of the tree. Called by Item's behavior machinery.
void prepareBehaviorAnimation(Animation* animation, Item* target,
                              const std::string& property, const std::any& to);

} // namespace detail

} // namespace Gui
} // namespace Firmir
