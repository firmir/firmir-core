#pragma once

#include <firmir/object.hpp>

#include <chrono>
#include <memory>

namespace Firmir {

/**
 * @brief Controls a dedicated OS thread running an event loop.
 *
 * A Thread instance is a controller object: it lives in the thread that
 * created it, while the work happens in the spawned thread. The event loop
 * dispatches posted tasks in strict FIFO order and services timers of all
 * objects bound to this thread.
 */
class Thread : public Object {
public:
    explicit Thread(Object* parent = nullptr);
    ~Thread() override;

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;
    Thread(Thread&&) = delete;
    Thread& operator=(Thread&&) = delete;

    /// Starts the worker thread: setup(), run(), cleanup().
    /// @throws std::logic_error when already running.
    void start();

    /// Asks the event loop to exit with code 0 and wakes it up. Thread-safe.
    void quit();
    /// Asks the event loop to exit with the given code. Thread-safe.
    void exit(int code = 0);

    /// Blocks until the worker thread finishes.
    bool wait();
    /// Blocks up to @p timeout; returns true when the thread finished.
    bool wait(std::chrono::milliseconds timeout);

    /// Returns true while the worker thread is running.
    bool isRunning() const;
    /// Returns true when the worker thread has finished (or never started).
    bool isFinished() const;

    /// Runs the event loop in the calling thread until quit()/exit().
    /// For a started Thread this must be called from its own worker thread.
    int exec();

    /// Returns the Thread managing the calling thread, or nullptr for raw threads.
    static Thread* currentThread();
    /// Returns the Thread object representing the main thread.
    static Thread* mainThread();

protected:
    /// Called in the worker thread before run().
    virtual void setup();
    /// Called in the worker thread; the default implementation enters exec().
    virtual void run();
    /// Called in the worker thread after run() returns.
    virtual void cleanup();

private:
    struct MainTag;
    explicit Thread(MainTag);

    class Impl;
    std::unique_ptr<Impl> d;

    friend struct ThreadBridgeAccess;
};

} // namespace Firmir
