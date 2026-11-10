#pragma once

#include <firmir/string.hpp>

#include <any>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

namespace Firmir {

class Thread;
class Object;

namespace detail {

/// Converts a std::any property value to the native type T: exact match
/// first, then the lenient conversions (arithmetic <-> arithmetic,
/// const char*/std::string/Firmir::String between the string types).
/// Throws std::invalid_argument when the conversion is impossible.
template <typename T>
T anyConvert(const std::any& value) {
    if (const T* exact = std::any_cast<T>(&value))
        return *exact;
    if constexpr (std::is_arithmetic_v<T>) {
        if (const auto* v = std::any_cast<double>(&value))
            return static_cast<T>(*v);
        if (const auto* v = std::any_cast<float>(&value))
            return static_cast<T>(*v);
        if (const auto* v = std::any_cast<int>(&value))
            return static_cast<T>(*v);
        if (const auto* v = std::any_cast<long long>(&value))
            return static_cast<T>(*v);
        if (const auto* v = std::any_cast<bool>(&value))
            return static_cast<T>(*v);
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (const auto* v = std::any_cast<const char*>(&value))
            return *v;
        if (const auto* v = std::any_cast<String>(&value))
            return v->toStdString();
        if (const auto* v = std::any_cast<double>(&value))
            return String::number(*v).toStdString();
        if (const auto* v = std::any_cast<float>(&value))
            return String::number(*v).toStdString();
        if (const auto* v = std::any_cast<int>(&value))
            return String::number(*v).toStdString();
        if (const auto* v = std::any_cast<long long>(&value))
            return String::number(static_cast<double>(*v)).toStdString();
        if (const auto* v = std::any_cast<bool>(&value))
            return std::string(*v ? "true" : "false");
    } else if constexpr (std::is_same_v<T, String>) {
        if (const auto* v = std::any_cast<const char*>(&value))
            return String(*v);
        if (const auto* v = std::any_cast<std::string>(&value))
            return String(*v);
        if (const auto* v = std::any_cast<double>(&value))
            return String::number(*v);
        if (const auto* v = std::any_cast<float>(&value))
            return String::number(*v);
        if (const auto* v = std::any_cast<int>(&value))
            return String::number(*v);
        if (const auto* v = std::any_cast<long long>(&value))
            return String::number(static_cast<double>(*v));
        if (const auto* v = std::any_cast<bool>(&value))
            return String(*v ? "true" : "false");
    }
    throw std::invalid_argument("Firmir::Object: property value type mismatch");
}

} // namespace detail

/// Delivery mode for a connected slot.
enum class ConnectionMode {
    Direct, ///< Invoke the slot immediately, in the emitting thread.
    Queued, ///< Post the invocation to the event queue of the receiver's thread.
    Auto    ///< Direct when emitter and receiver share a thread, Queued otherwise.
};

/// Base class for all events delivered to Object instances.
class Event {
public:
    enum Type : int { None = 0, User = 1000 };

    explicit Event(int type = None) noexcept;
    virtual ~Event();

    Event(const Event&) = delete;
    Event& operator=(const Event&) = delete;

    /// Returns the numeric event type.
    int type() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

/**
 * @brief Handle to an established signal/slot connection.
 *
 * Copyable lightweight handle; does not disconnect on destruction.
 * Use ScopedConnection for RAII semantics.
 */
class Connection {
public:
    Connection() = default;
    ~Connection();
    Connection(const Connection& other);
    Connection& operator=(const Connection& other);
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;

    /// Returns true when this handle refers to a connection record.
    bool valid() const noexcept;
    /// Disconnects the slot. Returns false if already disconnected or dead.
    bool disconnect();

private:
    struct Impl;
    explicit Connection(std::shared_ptr<Impl> impl) : d(std::move(impl)) {}
    std::shared_ptr<Impl> d;
    friend class Object;
};

/// RAII wrapper that disconnects its connection on destruction.
class ScopedConnection {
public:
    ScopedConnection() : d(std::make_unique<Impl>()) {}
    explicit ScopedConnection(Connection connection)
        : d(std::make_unique<Impl>()) {
        d->connection = std::move(connection);
    }
    ~ScopedConnection() {
        if (d) // null after a move
            d->connection.disconnect();
    }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;
    ScopedConnection(ScopedConnection&&) noexcept = default;
    ScopedConnection& operator=(ScopedConnection&&) noexcept = default;

private:
    // Header-only value wrapper; Impl is defined inline for the same reason
    // as in Component (no core .cpp linkage for a two-line type).
    struct Impl {
        Connection connection;
    };
    std::unique_ptr<Impl> d;
};

/**
 * @brief Base class with thread affinity, ownership hierarchy, named signals,
 *        event delivery, dynamic properties and per-object timers.
 *
 * A freshly constructed object binds to the current thread (or to the main
 * thread when constructed outside any managed thread). Destroying a parent
 * destroys its children. Thread-safe: connect/disconnect/emitSignal,
 * postEvent/deleteLater, property accessors; other methods require the owner
 * thread unless documented otherwise.
 */
class Object {
public:
    explicit Object(Object* parent = nullptr);
    virtual ~Object();

    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;

    /// Returns the parent object, or nullptr.
    Object* parent() const noexcept;
    /// Re-parents the object. Both objects must share the same thread.
    void setParent(Object* parent);
    /// Returns a snapshot of the direct children.
    std::vector<Object*> children() const;

    /// Sets the object name used by findChild()/findChildren().
    void setName(const std::string& name);
    /// Returns the object name.
    std::string name() const;

    /// Registers a non-owning named singleton for generated Firml code.
    /// The instance is unregistered automatically when destroyed.
    static void registerSingleton(const std::string& name, Object* instance);
    static void unregisterSingleton(const std::string& name, Object* instance);
    static Object* singleton(const std::string& name);
    template <typename T>
    static T* singleton(const std::string& name) {
        return dynamic_cast<T*>(singleton(name));
    }

    /// Finds the first child (recursively) castable to T with the given name.
    template <typename T = Object>
    T* findChild(const std::string& name = {}) const {
        static_assert(std::is_base_of_v<Object, T>, "T must derive from Object");
        for (Object* child : children()) {
            if (T* hit = dynamic_cast<T*>(child)) {
                if (name.empty() || child->name() == name)
                    return hit;
            }
            if (T* hit = child->findChild<T>(name))
                return hit;
        }
        return nullptr;
    }

    /// Finds all children (recursively) castable to T, optionally filtered by name.
    template <typename T = Object>
    std::vector<T*> findChildren(const std::string& name = {}) const {
        static_assert(std::is_base_of_v<Object, T>, "T must derive from Object");
        std::vector<T*> result;
        for (Object* child : children()) {
            if (T* hit = dynamic_cast<T*>(child)) {
                if (name.empty() || child->name() == name)
                    result.push_back(hit);
            }
            std::vector<T*> nested = child->findChildren<T>(name);
            result.insert(result.end(), nested.begin(), nested.end());
        }
        return result;
    }

    /// Signal emitted after a property value changed:
    /// (std::string name, std::any newValue).
    static constexpr auto signalPropertyChanged = "propertyChanged";
    /// Signal emitted (no arguments) at the start of the destructor, before
    /// the children are destroyed.
    static constexpr auto signalDestroyed = "destroyed";

    /// Metadata of a property registered with registerProperty().
    struct PropertyMeta {
        std::string name;
        const std::type_info* type = nullptr; ///< Native C++ type.
    };

    /**
     * @brief Registers a native property backed by member accessors.
     *
     * After registration setProperty()/property() dispatch to the given
     * accessors (with lenient type conversion on write) and the property is
     * listed in propertyMetaList(). The setter is responsible for emitting
     * notifyPropertyChanged() when the value actually changes.
     * Register from the constructor, before the object is shared.
     */
    template <typename T, typename C>
    void registerProperty(const std::string& name, T (C::*getter)() const,
                          void (C::*setter)(T)) {
        static_assert(std::is_base_of_v<Object, C>, "C must derive from Object");
        registerPropertyImpl(name, typeid(T),
                             [getter](const Object* object) -> std::any {
                                 return (static_cast<const C*>(object)->*getter)();
                             },
                             [setter](Object* object, const std::any& value) {
                                 (static_cast<C*>(object)->*setter)(
                                     detail::anyConvert<T>(value));
                             });
    }

    /// Type-erased registerProperty() for custom value conversion; the setter
    /// receives the raw std::any and must throw std::invalid_argument on a
    /// type mismatch.
    void registerPropertyImpl(const std::string& name, const std::type_info& type,
                              std::function<std::any(const Object*)> getter,
                              std::function<void(Object*, const std::any&)> setter);

    /// Returns the registered property metadata (registration order) followed
    /// by the dynamic property entries (sorted by name). Thread-safe.
    std::vector<PropertyMeta> propertyMetaList() const;

    /**
     * @brief Writes a property. Thread-safe.
     *
     * A registered property is written through its native setter (which emits
     * signalPropertyChanged when the value changes); any other name goes to
     * the dynamic property bag and emits signalPropertyChanged on every set.
     * @throws std::invalid_argument when a registered setter rejects the type.
     */
    void setProperty(const std::string& key, std::any value);
    /// Returns the property value or an empty std::any. Thread-safe.
    std::any property(const std::string& key) const;
    /// Alias for property().
    std::any getProperty(const std::string& key) const;
    /// Returns true when the property exists (registered or dynamic).
    /// Thread-safe.
    bool hasProperty(const std::string& key) const;

    /**
     * @brief Write interception hook for behaviors (GUI animations).
     *
     * When set, setProperty() first offers the write to the hook; a hook
     * returning true consumes it (the property is not written). Advanced API —
     * installed by Firmir::Gui::Item for Behavior support. Thread-safe.
     */
    using PropertyWriteHook =
        std::function<bool(Object&, const std::string&, const std::any&)>;
    void setPropertyWriteHook(PropertyWriteHook hook);
    /// Writes a property bypassing the write hook (used by animations to drive
    /// the property they animate).
    void setPropertyDirect(const std::string& key, std::any value);

    /// Returns the thread this object is bound to.
    Thread* thread() const noexcept;
    /**
     * @brief Moves the object to another thread.
     * @throws std::logic_error when the object has a parent or children, when
     *         called from a foreign thread, or for Thread instances.
     */
    void moveToThread(Thread* target);

    /**
     * @brief Connects a named signal of this object to a slot bound to @p receiver.
     *
     * The receiver determines the delivery thread for Queued/Auto modes.
     * Thread-safe.
     */
    template <typename... Args, typename F>
    Connection connect(Object* receiver, const std::string& signal, F&& slot,
                       ConnectionMode mode = ConnectionMode::Auto) {
        static_assert(std::is_invocable_v<F, Args...>,
                      "slot must be invocable with the signal arguments");
        std::function<void(Args...)> fn(std::forward<F>(slot));
        return connectImpl(signal, receiver, mode, std::any(std::move(fn)));
    }

    /// Connects a named signal to a slot bound to this object.
    template <typename... Args, typename F>
    Connection connect(const std::string& signal, F&& slot,
                       ConnectionMode mode = ConnectionMode::Direct) {
        return connect<Args...>(this, signal, std::forward<F>(slot), mode);
    }


    /**
     * @brief Emits a named signal, invoking all connected slots. Thread-safe.
     * @throws std::runtime_error when a slot signature does not match Args...
     */
    template <typename... Args>
    void emitSignal(const std::string& signal, Args&&... args) const {
        using Tuple = std::tuple<std::decay_t<Args>...>;
        using Fn = std::function<void(std::decay_t<Args>...)>;
        ErasedInvoke invoke = [](const std::any& fnAny, const void* packed) {
            const auto& fn = std::any_cast<const Fn&>(fnAny);
            const auto& tuple = *static_cast<const Tuple*>(packed);
            std::apply(fn, tuple);
        };
        ErasedClone clone = [](const void* packed) -> std::shared_ptr<void> {
            return std::make_shared<Tuple>(*static_cast<const Tuple*>(packed));
        };
        Tuple tuple(std::forward<Args>(args)...);
        emitImpl(signal, &tuple, invoke, clone);
    }

    /// Handles an event. Override to react. Returns true when handled.
    virtual bool event(Event& e);
    /// Filters events addressed to @p watched. Return true to consume the event.
    virtual bool eventFilter(Object* watched, Event& e);
    /// Installs an event filter object.
    void installEventFilter(Object* filter);
    /// Removes a previously installed event filter.
    void removeEventFilter(Object* filter);

    /// Delivers the event synchronously (filters first, then receiver->event()).
    static bool sendEvent(Object* receiver, Event& e);
    /// Posts the event to the receiver's thread queue (FIFO). Thread-safe.
    static void postEvent(Object* receiver, std::unique_ptr<Event> event);

    /// Starts a repeating per-object timer, returns its id. Owner thread only.
    int startTimer(std::chrono::milliseconds interval);
    /// Stops a per-object timer started with startTimer(). Owner thread only.
    void killTimer(int timerId);
    /// Called when a per-object timer fires. Override to react.
    virtual void timerEvent(int timerId);

    /// Schedules deferred deletion in the owner thread's event loop. Thread-safe.
    void deleteLater();

protected:
    /// Emits signalPropertyChanged(name, currentValue) — call from a native
    /// setter after a registered property actually changed. Thread-safe.
    void notifyPropertyChanged(const std::string& name);

    /// Internal constructor used by Thread to override automatic affinity.
    Object(Object* parent, Thread* affinity);

    /// Called by moveToThread() after the invariant checks and before the
    /// affinity changes. May throw to veto the move. Default: no-op.
    virtual void onThreadChanging(Thread* target);

    /// Used by Thread's destructor: the PIMPL member is destroyed before the
    /// Object base subobject, so the self-affinity must be detached first.
    /// An object without affinity is treated as being in its owner thread.
    void clearAffinity() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> d;

    using ErasedInvoke = std::function<void(const std::any&, const void*)>;
    using ErasedClone = std::function<std::shared_ptr<void>(const void*)>;

    Connection connectImpl(const std::string& signal, Object* receiver,
                           ConnectionMode mode, std::any slot);
    void emitImpl(const std::string& signal, const void* args,
                  const ErasedInvoke& invoke, const ErasedClone& clone) const;
    bool inOwnerThread() const;
    void requireOwnerThread(const char* what) const;

    friend class Thread;
    friend class Timer;
};

} // namespace Firmir

