#pragma once

#include <firmir/object.hpp>

#include <chrono>
#include <functional>
#include <memory>

namespace Firmir {

/**
 * @brief Timer bound to the event loop of its owner thread.
 *
 * Emits the "timeout" signal when it fires; connect slots with the regular
 * Object::connect() mechanism. start()/stop() may be called from any thread;
 * calls from a foreign thread are marshalled into the owner thread's queue.
 * The timer uses one timerfd registered in the owner thread's epoll set; no
 * extra thread is created per timer.
 */
class Timer : public Object {
public:
    explicit Timer(Object* parent = nullptr);
    ~Timer() override;

    Timer(const Timer&) = delete;
    Timer& operator=(const Timer&) = delete;
    Timer(Timer&&) = delete;
    Timer& operator=(Timer&&) = delete;

    /// Sets the interval; must be positive.
    void setInterval(std::chrono::milliseconds interval);
    /// Returns the current interval.
    std::chrono::milliseconds interval() const;

    /// Selects single-shot mode: the timer fires once and stops.
    void setSingleShot(bool singleShot);
    /// Returns true in single-shot mode.
    bool isSingleShot() const;

    /// Returns true while the timer is running. Thread-safe.
    bool isActive() const;

    /// Starts the timer with the configured interval. Thread-safe.
    void start();
    /// Sets the interval and starts the timer. Thread-safe.
    void start(std::chrono::milliseconds interval);
    /// Stops the timer. Thread-safe.
    void stop();

    /// Fires @p slot once after @p interval in the context object's thread.
    /// Must be called from the context object's thread.
    static void singleShot(std::chrono::milliseconds interval, Object* context,
                           std::function<void()> slot);

protected:
    /// Retargets the timerfd owner; vetoes the move while the timer is active.
    void onThreadChanging(Thread* target) override;

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace Firmir
