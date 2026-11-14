#include <firmir/gui/animation.hpp>

#include "detail/color_convert.hpp"
#include "detail/window_internal.hpp"

#include <firmir/gui/item.hpp>
#include <firmir/timer.hpp>

#include <algorithm>
#include <any>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace Firmir {
namespace Gui {

double applyEasing(Easing easing, double t) {
    t = std::clamp(t, 0.0, 1.0);
    switch (easing) {
    case Easing::Linear: return t;
    case Easing::InQuad: return t * t;
    case Easing::OutQuad: return t * (2.0 - t);
    case Easing::InOutQuad:
        return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
    case Easing::OutCubic: {
        const double u = t - 1.0;
        return u * u * u + 1.0;
    }
    case Easing::OutExpo: return t >= 1.0 ? 1.0 : 1.0 - std::pow(2.0, -10.0 * t);
    }
    return t;
}

namespace {

double propertyAsDouble(Object* object, const std::string& property) {
    const std::any value = object->getProperty(property);
    if (const auto* v = std::any_cast<double>(&value))
        return *v;
    if (const auto* v = std::any_cast<float>(&value))
        return static_cast<double>(*v);
    if (const auto* v = std::any_cast<int>(&value))
        return static_cast<double>(*v);
    if (const auto* v = std::any_cast<long long>(&value))
        return static_cast<double>(*v);
    if (const auto* v = std::any_cast<bool>(&value))
        return *v ? 1.0 : 0.0;
    return 0.0;
}

Color propertyAsColor(Object* object, const std::string& property) {
    return detail::anyToColor(object->getProperty(property));
}

/// Interval of the shared animation clock. Deliberately faster than common
/// display refresh rates (60–120 Hz): positions are computed from the wall
/// clock, so a clock that outruns the display means every presented frame
/// shows the freshest position, while a slower clock (16 ms on a 60 Hz panel)
/// makes a frame occasionally advance two ticks at once — a visible hitch.
/// The extra ticks only cost a few property writes: the render thread
/// coalesces frame requests and the display backend paces presentation.
constexpr auto kFrameInterval = std::chrono::milliseconds(8);

/**
 * Shared frame driver: one timer per thread advances every running animation.
 *
 * Driving all animations from a single clock (instead of one timer per
 * animation) keeps the properties of a frame consistent: with independent
 * timers an item animated in x and y got a frame with only x changed
 * followed by a frame with only y, which reads as a jerky, stair-stepping
 * diagonal move. The driver also opens a frame batch around the tick, so a
 * window is rendered once per tick with every property already applied.
 */
class AnimationFrameDriver {
public:
    static AnimationFrameDriver& instance() {
        // Intentionally leaked (like Platform::instance): the clock lives as
        // long as the thread, so animations destroyed late never touch a
        // half-destroyed driver.
        static thread_local AnimationFrameDriver* driver =
            new AnimationFrameDriver;
        return *driver;
    }

    void add(Animation* animation) {
        if (std::find(d->animations.begin(), d->animations.end(), animation) !=
            d->animations.end())
            return;
        d->animations.push_back(animation);
        ensureTimer().start();
    }

    void remove(Animation* animation) {
        auto& list = d->animations;
        list.erase(std::remove(list.begin(), list.end(), animation),
                   list.end());
        if (list.empty() && d->timer)
            d->timer->stop();
    }

private:
    Timer& ensureTimer() {
        if (!d->timer) {
            // No parent: the driver owns it for the lifetime of the thread's
            // event loop (animations outlive individual windows).
            d->timer = new Timer;
            d->timer->setInterval(kFrameInterval);
            d->timer->connect(d->timer, "timeout", [this] { tick(); },
                              ConnectionMode::Direct);
        }
        return *d->timer;
    }

    void tick() {
        // Snapshot: finishing, stopping or destroying an animation mutates
        // the list (a callback may even delete items during the tick).
        const std::vector<Animation*> snapshot = d->animations;
        detail::beginFrameBatch();
        for (Animation* animation : snapshot) {
            auto& list = d->animations;
            if (std::find(list.begin(), list.end(), animation) == list.end())
                continue; // gone since the snapshot: only compare, never deref
            if (animation->running())
                animation->advance();
        }
        detail::endFrameBatch();
        auto& list = d->animations;
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [](Animation* a) { return !a->running(); }),
                   list.end());
        if (list.empty())
            d->timer->stop();
    }

    class Data {
    public:
        std::vector<Animation*> animations;
        Timer* timer = nullptr;
    };
    std::unique_ptr<Data> d = std::make_unique<Data>();
};

} // namespace

// ------------------------------- Animation --------------------------------

class Animation::Impl {
public:
    bool running = false;
};

Animation::Animation(Object* parent) : Object(parent), d(std::make_unique<Impl>()) {}
Animation::~Animation() { AnimationFrameDriver::instance().remove(this); }

bool Animation::running() const { return d->running; }

void Animation::start() {
    if (d->running)
        return;
    d->running = true;
    // The start value write is part of the frame: keep it in a batch so the
    // first frame of the animation is not rendered half-applied.
    detail::beginFrameBatch();
    onStart();
    detail::endFrameBatch();
    // onStart() may finish() immediately (no target/property): only a still
    // running animation joins the frame driver.
    if (d->running)
        AnimationFrameDriver::instance().add(this);
}

void Animation::stop() {
    if (!d->running)
        return;
    d->running = false;
    AnimationFrameDriver::instance().remove(this);
    onStop();
}

void Animation::advance() {}

std::vector<Animation*> Animation::childAnimations() const {
    std::vector<Animation*> result;
    for (Object* child : children())
        if (Animation* animation = dynamic_cast<Animation*>(child))
            result.push_back(animation);
    return result;
}

void Animation::onStart() {}
void Animation::onStop() {}

void Animation::finish() {
    if (!d->running)
        return;
    d->running = false;
    AnimationFrameDriver::instance().remove(this);
    emitSignal(signalFinished);
}

// ---------------------------- PropertyAnimation ---------------------------

class PropertyAnimation::Impl {
public:
    Item* target = nullptr;
    std::string property;
    double from = 0;
    bool hasFrom = false;
    double to = 0;
    std::chrono::milliseconds duration{250};
    Easing easing = Easing::Linear;

    Connection targetDestroyed;
    std::chrono::steady_clock::time_point started;
    double startValue = 0;
};

PropertyAnimation::PropertyAnimation(Object* parent)
    : Animation(parent), d(std::make_unique<Impl>()) {}

PropertyAnimation::PropertyAnimation(Item* target, std::string property,
                                     Object* parent)
    : PropertyAnimation(parent) {
    setTargetItem(target);
    setTargetProperty(std::move(property));
}

PropertyAnimation::~PropertyAnimation() { d->targetDestroyed.disconnect(); }

Item* PropertyAnimation::target() const { return d->target; }

void PropertyAnimation::setTargetItem(Item* target) {
    d->targetDestroyed.disconnect();
    d->target = target;
    if (target)
        d->targetDestroyed = target->connect(
            this, signalDestroyed, [this] { stop(); }, ConnectionMode::Direct);
}

std::string PropertyAnimation::property() const { return d->property; }
void PropertyAnimation::setTargetProperty(const std::string& property) {
    d->property = property;
}
void PropertyAnimation::setFrom(double from) {
    d->from = from;
    d->hasFrom = true;
}
double PropertyAnimation::from() const { return d->from; }
bool PropertyAnimation::hasFrom() const { return d->hasFrom; }
void PropertyAnimation::setTo(double to) { d->to = to; }
double PropertyAnimation::to() const { return d->to; }
void PropertyAnimation::setDuration(std::chrono::milliseconds duration) {
    d->duration = std::max(duration, std::chrono::milliseconds(1));
}
std::chrono::milliseconds PropertyAnimation::duration() const { return d->duration; }
void PropertyAnimation::setEasing(Easing easing) { d->easing = easing; }
Easing PropertyAnimation::easing() const { return d->easing; }

void PropertyAnimation::onStart() {
    if (!d->target || d->property.empty()) {
        finish();
        return;
    }
    d->startValue = d->hasFrom ? d->from : propertyAsDouble(d->target, d->property);
    d->started = std::chrono::steady_clock::now();
    advance();
}

void PropertyAnimation::advance() {
    if (!running())
        return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - d->started);
    const double t = static_cast<double>(elapsed.count()) / d->duration.count();
    if (t >= 1.0) {
        d->target->setPropertyDirect(d->property, std::any(d->to));
        finish();
        return;
    }
    const double value =
        d->startValue + (d->to - d->startValue) * applyEasing(d->easing, t);
    d->target->setPropertyDirect(d->property, std::any(value));
}

// ----------------------------- ColorAnimation -----------------------------

class ColorAnimation::Impl {
public:
    Item* target = nullptr;
    std::string property;
    Color to;
    std::chrono::milliseconds duration{250};
    Easing easing = Easing::Linear;

    Connection targetDestroyed;
    std::chrono::steady_clock::time_point started;
    Color startValue;
};

ColorAnimation::ColorAnimation(Object* parent)
    : Animation(parent), d(std::make_unique<Impl>()) {}

ColorAnimation::~ColorAnimation() { d->targetDestroyed.disconnect(); }

Item* ColorAnimation::target() const { return d->target; }

void ColorAnimation::setTargetItem(Item* target) {
    d->targetDestroyed.disconnect();
    d->target = target;
    if (target)
        d->targetDestroyed = target->connect(
            this, signalDestroyed, [this] { stop(); }, ConnectionMode::Direct);
}

std::string ColorAnimation::property() const { return d->property; }
void ColorAnimation::setTargetProperty(const std::string& property) {
    d->property = property;
}
void ColorAnimation::setTo(Color to) { d->to = to; }
Color ColorAnimation::to() const { return d->to; }
void ColorAnimation::setDuration(std::chrono::milliseconds duration) {
    d->duration = std::max(duration, std::chrono::milliseconds(1));
}
std::chrono::milliseconds ColorAnimation::duration() const { return d->duration; }
void ColorAnimation::setEasing(Easing easing) { d->easing = easing; }
Easing ColorAnimation::easing() const { return d->easing; }

void ColorAnimation::onStart() {
    if (!d->target || d->property.empty()) {
        finish();
        return;
    }
    d->startValue = propertyAsColor(d->target, d->property);
    d->started = std::chrono::steady_clock::now();
    advance();
}

void ColorAnimation::advance() {
    if (!running())
        return;
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - d->started);
    const double t = static_cast<double>(elapsed.count()) / d->duration.count();
    if (t >= 1.0) {
        d->target->setPropertyDirect(d->property, std::any(d->to));
        finish();
        return;
    }
    const double k = applyEasing(d->easing, t);
    const auto mix = [k](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(
            std::clamp(std::lround(a + (b - a) * k), 0l, 255l));
    };
    const Color value = Color::rgba(mix(d->startValue.r, d->to.r),
                                    mix(d->startValue.g, d->to.g),
                                    mix(d->startValue.b, d->to.b),
                                    mix(d->startValue.a, d->to.a));
    d->target->setPropertyDirect(d->property, std::any(value));
}

// ----------------------------- PauseAnimation -----------------------------

class PauseAnimation::Impl {
public:
    std::chrono::milliseconds duration{250};
};

PauseAnimation::PauseAnimation(Object* parent)
    : Animation(parent), d(std::make_unique<Impl>()) {}
PauseAnimation::~PauseAnimation() = default;

void PauseAnimation::setDuration(std::chrono::milliseconds duration) {
    d->duration = std::max(duration, std::chrono::milliseconds(1));
}
std::chrono::milliseconds PauseAnimation::duration() const { return d->duration; }

void PauseAnimation::onStart() {
    Timer::singleShot(d->duration, this, [this] { finish(); });
}

// --------------------------- SequentialAnimation --------------------------

class SequentialAnimation::Impl {
public:
    std::vector<Animation*> kids;
    std::vector<Connection> kidConnections;
    int current = -1;
};

SequentialAnimation::SequentialAnimation(Object* parent)
    : Animation(parent), d(std::make_unique<Impl>()) {}
SequentialAnimation::~SequentialAnimation() = default;

void SequentialAnimation::onStart() {
    if (d->kidConnections.empty()) {
        // Children are added before the first start(); subscribe once.
        d->kids = childAnimations();
        int index = 0;
        for (Animation* kid : d->kids) {
            d->kidConnections.push_back(kid->connect(
                this, signalFinished,
                [this, index] {
                    if (!running() || index != d->current)
                        return;
                    ++d->current;
                    if (d->current >= static_cast<int>(d->kids.size())) {
                        finish();
                        return;
                    }
                    d->kids[d->current]->start();
                },
                ConnectionMode::Direct));
            ++index;
        }
    }
    if (d->kids.empty()) {
        finish();
        return;
    }
    d->current = 0;
    d->kids[0]->start();
}

void SequentialAnimation::onStop() {
    if (d->current >= 0 && d->current < static_cast<int>(d->kids.size()))
        d->kids[d->current]->stop();
    d->current = -1;
}

// ---------------------------- ParallelAnimation ---------------------------

class ParallelAnimation::Impl {
public:
    std::vector<Animation*> kids;
    std::vector<Connection> kidConnections;
    int remaining = 0;
};

ParallelAnimation::ParallelAnimation(Object* parent)
    : Animation(parent), d(std::make_unique<Impl>()) {}
ParallelAnimation::~ParallelAnimation() = default;

void ParallelAnimation::onStart() {
    if (d->kidConnections.empty()) {
        d->kids = childAnimations();
        for (Animation* kid : d->kids) {
            d->kidConnections.push_back(kid->connect(
                this, signalFinished,
                [this] {
                    if (!running())
                        return;
                    if (--d->remaining <= 0)
                        finish();
                },
                ConnectionMode::Direct));
        }
    }
    if (d->kids.empty()) {
        finish();
        return;
    }
    d->remaining = static_cast<int>(d->kids.size());
    for (Animation* kid : d->kids)
        kid->start();
}

void ParallelAnimation::onStop() {
    for (Animation* kid : d->kids)
        kid->stop();
    d->remaining = 0;
}

// ------------------------- behavior injection -----------------------------

namespace detail {

void prepareBehaviorAnimation(Animation* animation, Item* target,
                              const std::string& property, const std::any& to) {
    if (auto* numeric = dynamic_cast<PropertyAnimation*>(animation)) {
        numeric->setTargetItem(target);
        numeric->setTargetProperty(property);
        numeric->setTo(Firmir::detail::anyConvert<double>(to));
    } else if (auto* color = dynamic_cast<ColorAnimation*>(animation)) {
        color->setTargetItem(target);
        color->setTargetProperty(property);
        color->setTo(anyToColor(to));
    }
    for (Animation* child : animation->childAnimations())
        prepareBehaviorAnimation(child, target, property, to);
}

} // namespace detail

} // namespace Gui
} // namespace Firmir


