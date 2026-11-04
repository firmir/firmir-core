#pragma once

#include <chrono>
#include <future>
#include <memory>
#include <type_traits>
#include <utility>

namespace Firmir {

/// Firmir analogue of QFuture<T>: a handle to a result computed on
/// another thread. Copyable; all copies observe the same result.
///
/// Note: as a template, Future must define its Impl in the header; the
/// state is still held behind a pointer like in every other public class.
template <typename T> class Future {
public:
    Future() : d(std::make_unique<Impl>()) {}
    Future(const Future& other) : d(std::make_unique<Impl>(*other.d)) {}
    Future(Future&&) noexcept = default;
    Future& operator=(const Future& other) {
        if (this != &other)
            d->state = other.d->state;
        return *this;
    }
    Future& operator=(Future&&) noexcept = default;

    /// Wraps an existing std::future (used by Concurrent::run).
    explicit Future(std::future<T>&& future) : d(std::make_unique<Impl>()) {
        d->state = std::move(future);
    }

    bool isValid() const { return d->state.valid(); }

    bool isFinished() const {
        return isValid() &&
               d->state.wait_for(std::chrono::seconds(0)) ==
                   std::future_status::ready;
    }

    void waitForFinished() {
        if (isValid())
            d->state.wait();
    }

    /// Blocks until the result is ready and returns a copy.
    T result() {
        waitForFinished();
        return d->state.get();
    }

private:
    // Template classes cannot hide Impl in a .cpp; the pointer indirection
    // is kept anyway for uniformity with the rest of the framework.
    struct Impl {
        std::shared_future<T> state;
    };
    std::unique_ptr<Impl> d;
};

/// void specialization of Future.
template <> class Future<void> {
public:
    Future() : d(std::make_unique<Impl>()) {}
    Future(const Future& other) : d(std::make_unique<Impl>(*other.d)) {}
    Future(Future&&) noexcept = default;
    Future& operator=(const Future& other) {
        if (this != &other)
            d->state = other.d->state;
        return *this;
    }
    Future& operator=(Future&&) noexcept = default;

    explicit Future(std::future<void>&& future) : d(std::make_unique<Impl>()) {
        d->state = std::move(future);
    }

    bool isValid() const { return d->state.valid(); }

    bool isFinished() const {
        return isValid() &&
               d->state.wait_for(std::chrono::seconds(0)) ==
                   std::future_status::ready;
    }

    void waitForFinished() {
        if (isValid())
            d->state.wait();
    }

    void result() {
        waitForFinished();
        d->state.get(); // rethrows a stored exception, if any
    }

private:
    struct Impl {
        std::shared_future<void> state;
    };
    std::unique_ptr<Impl> d;
};

/// Firmir analogue of the QtConcurrent namespace.
namespace Concurrent {

/// Analogue of QtConcurrent::run: invokes f(args...) on a worker thread
/// and returns a Future for the result. Each call spawns a fresh thread
/// (unlike Qt's shared thread pool).
template <typename F, typename... Args>
auto run(F&& f, Args&&... args)
    -> Future<std::invoke_result_t<F, Args...>> {
    using R = std::invoke_result_t<F, Args...>;
    return Future<R>(std::async(std::launch::async, std::forward<F>(f),
                                std::forward<Args>(args)...));
}

} // namespace Concurrent

} // namespace Firmir
