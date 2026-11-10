#include <firmir/object.hpp>

#include <firmir/log.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include "detail/bridge.hpp"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Firmir {

struct Event::Impl {
    int type;
};

Event::Event(int type) noexcept : d(std::make_unique<Impl>()) {
    d->type = type;
}

Event::~Event() = default;

int Event::type() const noexcept { return d->type; }

namespace {

const LoggingCategory& lcObject() {
    static const LoggingCategory category("firmir.object");
    return category;
}

struct Slot {
    std::uint64_t id = 0;
    std::string signal;
    Object* context = nullptr;
    std::weak_ptr<std::atomic<bool>> contextAlive;
    ConnectionMode mode = ConnectionMode::Auto;
    std::any fn;
};

std::mutex g_singletonMutex;
std::unordered_map<std::string, Object*> g_singletons;

struct SignalTable {
    std::uint64_t add(const std::string& signal, Slot slot) {
        std::lock_guard<std::mutex> lk(m);
        const std::uint64_t id = nextId++;
        slot.id = id;
        slots.emplace(id, std::move(slot));
        byName[signal].push_back(id);
        return id;
    }

    bool remove(std::uint64_t id) {
        std::lock_guard<std::mutex> lk(m);
        auto it = slots.find(id);
        if (it == slots.end())
            return false;
        auto nameIt = byName.find(it->second.signal);
        if (nameIt != byName.end()) {
            auto& ids = nameIt->second;
            for (auto pos = ids.begin(); pos != ids.end(); ++pos) {
                if (*pos == id) {
                    ids.erase(pos);
                    break;
                }
            }
            if (ids.empty())
                byName.erase(nameIt);
        }
        slots.erase(it);
        return true;
    }

    std::vector<Slot> snapshot(const std::string& signal) const {
        std::vector<Slot> result;
        std::lock_guard<std::mutex> lk(m);
        auto it = byName.find(signal);
        if (it == byName.end())
            return result;
        result.reserve(it->second.size());
        for (std::uint64_t id : it->second) {
            auto slotIt = slots.find(id);
            if (slotIt != slots.end())
                result.push_back(slotIt->second);
        }
        return result;
    }

    mutable std::mutex m;
    std::uint64_t nextId = 1;
    std::unordered_map<std::uint64_t, Slot> slots;
    std::unordered_map<std::string, std::vector<std::uint64_t>> byName;
};

} // namespace

struct Connection::Impl {
    std::weak_ptr<SignalTable> table;
    std::uint64_t id = 0;
};

Connection::~Connection() = default;
Connection::Connection(const Connection&) = default;
Connection& Connection::operator=(const Connection&) = default;
Connection::Connection(Connection&&) noexcept = default;
Connection& Connection::operator=(Connection&&) noexcept = default;

bool Connection::valid() const noexcept { return d != nullptr; }

bool Connection::disconnect() {
    const std::shared_ptr<Impl> impl = d;
    if (!impl)
        return false;
    const std::shared_ptr<SignalTable> table = impl->table.lock();
    if (!table)
        return false;
    return table->remove(impl->id);
}

class Object::Impl {
public:
    Object* q = nullptr;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<SignalTable> signals = std::make_shared<SignalTable>();
    std::atomic<Thread*> affinity{nullptr};
    std::atomic<Object*> parent{nullptr};

    std::mutex childrenMutex;
    std::vector<Object*> children;

    mutable std::mutex nameMutex;
    std::string name;

    mutable std::mutex propMutex;
    std::unordered_map<std::string, std::any> properties;

    // Registered (native) properties; append-only after construction.
    struct RegisteredProperty {
        std::string name;
        const std::type_info* type;
        std::function<std::any(const Object*)> getter;
        std::function<void(Object*, const std::any&)> setter;
    };
    std::vector<RegisteredProperty> registeredProps;
    std::unordered_map<std::string, size_t> registeredIndex;
    PropertyWriteHook writeHook;

    std::mutex filterMutex;
    std::vector<Object*> filters;

    // Owner thread only.
    std::unordered_map<int, Timer*> timers;
    int nextTimerId = 1;
};

Object::Object(Object* parent) : Object(parent, nullptr) {}

Object::Object(Object* parentObj, Thread* affinityOverride)
    : d(std::make_unique<Impl>()) {
    d->q = this;
    Thread* affinity = affinityOverride;
    if (parentObj) {
        Thread* parentThread = parentObj->thread();
        if (ThreadBridgeAccess::executionId(parentThread) != std::this_thread::get_id())
            throw std::logic_error(
                "Firmir::Object: a child must be created in its parent's thread");
        if (affinity && affinity != parentThread)
            throw std::logic_error(
                "Firmir::Object: parent and child must share the same thread");
        affinity = parentThread;
        std::lock_guard<std::mutex> lk(parentObj->d->childrenMutex);
        parentObj->d->children.push_back(this);
        d->parent.store(parentObj);
    }
    if (!affinity) {
        affinity = Thread::currentThread();
        if (!affinity)
            affinity = Thread::mainThread();
    }
    d->affinity.store(affinity);
}

Object::~Object() {
    if (!inOwnerThread()) {
        cLogError(lcObject())
            << "destroyed from a foreign thread; use deleteLater() instead";
        std::abort();
    }
    emitSignal(signalDestroyed);
    d->alive->store(false);

    std::vector<Object*> childrenCopy;
    {
        std::lock_guard<std::mutex> lk(d->childrenMutex);
        childrenCopy.swap(d->children);
    }
    for (Object* child : childrenCopy) {
        child->d->parent.store(nullptr);
        delete child;
    }
    for (auto& [id, timer] : d->timers) {
        (void)id;
        delete timer;
    }
    d->timers.clear();

    Object* parentObj = d->parent.load();
    if (parentObj) {
        std::lock_guard<std::mutex> lk(parentObj->d->childrenMutex);
        auto& siblings = parentObj->d->children;
        for (auto it = siblings.begin(); it != siblings.end(); ++it) {
            if (*it == this) {
                siblings.erase(it);
                break;
            }
        }
    }
}

Object* Object::parent() const noexcept { return d->parent.load(); }

void Object::setParent(Object* parentObj) {
    requireOwnerThread("setParent");
    if (parentObj == d->parent.load())
        return;
    if (parentObj == this)
        throw std::logic_error("Firmir::Object::setParent: cannot parent to self");
    if (parentObj && parentObj->thread() != thread())
        throw std::logic_error(
            "Firmir::Object::setParent: parent and child must share the same thread");
    Object* oldParent = d->parent.load();
    if (oldParent) {
        std::lock_guard<std::mutex> lk(oldParent->d->childrenMutex);
        auto& siblings = oldParent->d->children;
        for (auto it = siblings.begin(); it != siblings.end(); ++it) {
            if (*it == this) {
                siblings.erase(it);
                break;
            }
        }
    }
    d->parent.store(parentObj);
    if (parentObj) {
        std::lock_guard<std::mutex> lk(parentObj->d->childrenMutex);
        parentObj->d->children.push_back(this);
    }
    if (oldParent)
        oldParent->emitSignal("childrenChanged");
    if (parentObj)
        parentObj->emitSignal("childrenChanged");
}

std::vector<Object*> Object::children() const {
    std::lock_guard<std::mutex> lk(d->childrenMutex);
    return d->children;
}


void Object::registerSingleton(const std::string& name, Object* instance) {
    if (!instance)
        throw std::invalid_argument("Firmir::Object::registerSingleton: null instance");
    {
        std::lock_guard<std::mutex> lock(g_singletonMutex);
        if (!g_singletons.emplace(name, instance).second)
            throw std::logic_error("Firmir::Object::registerSingleton: duplicate name '" +
                                   name + "'");
    }
    instance->connect(
        instance, signalDestroyed,
        [name, instance] { unregisterSingleton(name, instance); },
        ConnectionMode::Direct);
}

void Object::unregisterSingleton(const std::string& name, Object* instance) {
    std::lock_guard<std::mutex> lock(g_singletonMutex);
    auto it = g_singletons.find(name);
    if (it != g_singletons.end() && it->second == instance)
        g_singletons.erase(it);
}

Object* Object::singleton(const std::string& name) {
    std::lock_guard<std::mutex> lock(g_singletonMutex);
    const auto it = g_singletons.find(name);
    return it == g_singletons.end() ? nullptr : it->second;
}

void Object::setName(const std::string& name) {
    std::lock_guard<std::mutex> lk(d->nameMutex);
    d->name = name;
}

std::string Object::name() const {
    std::lock_guard<std::mutex> lk(d->nameMutex);
    return d->name;
}

void Object::registerPropertyImpl(const std::string& name,
                                  const std::type_info& type,
                                  std::function<std::any(const Object*)> getter,
                                  std::function<void(Object*, const std::any&)> setter) {
    if (!getter || !setter)
        throw std::invalid_argument(
            "Firmir::Object::registerProperty: accessors must be callable");
    std::lock_guard<std::mutex> lk(d->propMutex);
    if (d->registeredIndex.count(name))
        throw std::invalid_argument(
            "Firmir::Object::registerProperty: '" + name + "' is already registered");
    d->registeredIndex.emplace(name, d->registeredProps.size());
    d->registeredProps.push_back(
        Impl::RegisteredProperty{name, &type, std::move(getter), std::move(setter)});
}

std::vector<Object::PropertyMeta> Object::propertyMetaList() const {
    std::lock_guard<std::mutex> lk(d->propMutex);
    std::vector<PropertyMeta> result;
    result.reserve(d->registeredProps.size() + d->properties.size());
    for (const Impl::RegisteredProperty& prop : d->registeredProps)
        result.push_back(PropertyMeta{prop.name, prop.type});
    for (const auto& [key, value] : d->properties)
        result.push_back(PropertyMeta{key, &value.type()});
    std::sort(result.begin() + static_cast<std::ptrdiff_t>(d->registeredProps.size()),
              result.end(), [](const PropertyMeta& a, const PropertyMeta& b) {
                  return a.name < b.name;
              });
    return result;
}

void Object::setPropertyWriteHook(PropertyWriteHook hook) {
    std::lock_guard<std::mutex> lk(d->propMutex);
    d->writeHook = std::move(hook);
}

void Object::setProperty(const std::string& key, std::any value) {
    PropertyWriteHook hook;
    {
        std::lock_guard<std::mutex> lk(d->propMutex);
        hook = d->writeHook;
    }
    if (hook && hook(*this, key, value))
        return; // consumed (e.g. a Behavior started an animation instead)
    setPropertyDirect(key, std::move(value));
}

void Object::setPropertyDirect(const std::string& key, std::any value) {
    {
        std::unique_lock<std::mutex> lk(d->propMutex);
        auto it = d->registeredIndex.find(key);
        if (it != d->registeredIndex.end()) {
            // Dispatch outside the lock: the native setter may emit
            // propertyChanged, whose slots may touch this object again.
            const auto setter = d->registeredProps[it->second].setter;
            lk.unlock();
            setter(this, value);
            return;
        }
        d->properties[key] = value;
    }
    emitSignal(signalPropertyChanged, key, value);
}

std::any Object::property(const std::string& key) const {
    std::function<std::any(const Object*)> getter;
    {
        std::lock_guard<std::mutex> lk(d->propMutex);
        auto regIt = d->registeredIndex.find(key);
        if (regIt != d->registeredIndex.end())
            getter = d->registeredProps[regIt->second].getter;
        else {
            auto it = d->properties.find(key);
            if (it == d->properties.end())
                return {};
            return it->second;
        }
    }
    return getter(this);
}

std::any Object::getProperty(const std::string& key) const { return property(key); }

bool Object::hasProperty(const std::string& key) const {
    std::lock_guard<std::mutex> lk(d->propMutex);
    return d->registeredIndex.count(key) != 0 ||
           d->properties.find(key) != d->properties.end();
}

void Object::notifyPropertyChanged(const std::string& name) {
    emitSignal(signalPropertyChanged, name, property(name));
}

Thread* Object::thread() const noexcept { return d->affinity.load(); }

bool Object::inOwnerThread() const {
    Thread* owner = d->affinity.load();
    if (!owner)
        return true; // No affinity (only possible during Thread teardown).
    return ThreadBridgeAccess::executionId(owner) == std::this_thread::get_id();
}

void Object::requireOwnerThread(const char* what) const {
    if (!inOwnerThread()) {
        std::string message = "Firmir::Object::";
        message += what;
        message += ": must be called from the owner thread";
        throw std::logic_error(message);
    }
}

void Object::moveToThread(Thread* target) {
    if (!target)
        throw std::invalid_argument("Firmir::Object::moveToThread: null target");
    if (dynamic_cast<Thread*>(this) != nullptr)
        throw std::logic_error("Firmir::Object::moveToThread: a Thread cannot be moved");
    if (d->parent.load() != nullptr)
        throw std::logic_error("Firmir::Object::moveToThread: object has a parent");
    if (!children().empty())
        throw std::logic_error("Firmir::Object::moveToThread: object has children");
    if (!d->timers.empty())
        throw std::logic_error("Firmir::Object::moveToThread: object has active timers");
    requireOwnerThread("moveToThread");
    onThreadChanging(target);
    d->affinity.store(target);
}

void Object::onThreadChanging(Thread* /*target*/) {}

void Object::clearAffinity() noexcept { d->affinity.store(nullptr); }


Connection Object::connectImpl(const std::string& signal, Object* receiver,
                               ConnectionMode mode, std::any slotFn) {
    if (!receiver)
        throw std::invalid_argument("Firmir::Object::connect: null receiver");
    Slot slot;
    slot.signal = signal;
    slot.context = receiver;
    slot.contextAlive = receiver->d->alive;
    slot.mode = mode;
    slot.fn = std::move(slotFn);
    const std::uint64_t id = d->signals->add(signal, std::move(slot));
    auto impl = std::make_shared<Connection::Impl>();
    impl->table = d->signals;
    impl->id = id;
    return Connection(std::move(impl));
}

void Object::emitImpl(const std::string& signal, const void* args,
                      const ErasedInvoke& invoke, const ErasedClone& clone) const {
    const std::vector<Slot> snapshot = d->signals->snapshot(signal);
    for (const Slot& slot : snapshot) {
        Object* context = slot.context;
        std::shared_ptr<std::atomic<bool>> alive = slot.contextAlive.lock();
        if (!alive || !alive->load())
            continue;

        bool direct = false;
        switch (slot.mode) {
        case ConnectionMode::Direct:
            direct = true;
            break;
        case ConnectionMode::Queued:
            direct = false;
            break;
        case ConnectionMode::Auto:
            direct = Thread::currentThread() == context->thread();
            break;
        }

        if (direct) {
            try {
                invoke(slot.fn, args);
            } catch (const std::bad_any_cast&) {
                throw std::runtime_error(
                    "Firmir::Object::emitSignal: slot signature mismatch on signal '" +
                    signal + "'");
            }
        } else {
            std::shared_ptr<void> argsCopy = clone(args);
            Thread* target = context->thread();
            ThreadBridgeAccess::postTask(
                target, [alive = std::move(alive), fn = slot.fn, invoke,
                         argsCopy = std::move(argsCopy)]() mutable {
                    if (!alive->load())
                        return;
                    try {
                        invoke(fn, argsCopy.get());
                    } catch (const std::bad_any_cast&) {
                        cLogError(lcObject())
                            << "queued slot signature mismatch";
                        std::abort();
                    }
                });
        }
    }
}


bool Object::event(Event&) { return false; }

bool Object::eventFilter(Object*, Event&) { return false; }

void Object::installEventFilter(Object* filter) {
    if (!filter)
        throw std::invalid_argument("Firmir::Object::installEventFilter: null filter");
    std::lock_guard<std::mutex> lk(d->filterMutex);
    d->filters.push_back(filter);
}

void Object::removeEventFilter(Object* filter) {
    std::lock_guard<std::mutex> lk(d->filterMutex);
    for (auto it = d->filters.begin(); it != d->filters.end(); ++it) {
        if (*it == filter) {
            d->filters.erase(it);
            return;
        }
    }
}

bool Object::sendEvent(Object* receiver, Event& e) {
    if (!receiver)
        return false;
    std::vector<Object*> filters;
    {
        std::lock_guard<std::mutex> lk(receiver->d->filterMutex);
        filters = receiver->d->filters;
    }
    for (auto it = filters.rbegin(); it != filters.rend(); ++it) {
        Object* filter = *it;
        if (!filter->d->alive->load())
            continue;
        if (filter->eventFilter(receiver, e))
            return true;
    }
    return receiver->event(e);
}

void Object::postEvent(Object* receiver, std::unique_ptr<Event> event) {
    if (!receiver || !event)
        throw std::invalid_argument("Firmir::Object::postEvent: null argument");
    std::weak_ptr<std::atomic<bool>> alive = receiver->d->alive;
    std::shared_ptr<Event> sharedEvent = std::move(event);
    ThreadBridgeAccess::postTask(
        receiver->thread(),
        [alive = std::move(alive), receiver, event = std::move(sharedEvent)]() mutable {
            std::shared_ptr<std::atomic<bool>> guard = alive.lock();
            if (!guard || !guard->load())
                return;
            sendEvent(receiver, *event);
        });
}

int Object::startTimer(std::chrono::milliseconds interval) {
    requireOwnerThread("startTimer");
    auto* timer = new Timer();
    timer->setInterval(interval);
    const int id = d->nextTimerId++;
    d->timers.emplace(id, timer);
    timer->connect(this, "timeout", [this, id] { timerEvent(id); },
                   ConnectionMode::Direct);
    timer->start();
    return id;
}

void Object::killTimer(int timerId) {
    requireOwnerThread("killTimer");
    auto it = d->timers.find(timerId);
    if (it == d->timers.end())
        return;
    delete it->second;
    d->timers.erase(it);
}

void Object::timerEvent(int) {}

void Object::deleteLater() {
    std::weak_ptr<std::atomic<bool>> alive = d->alive;
    ThreadBridgeAccess::postTask(
        thread(), [alive = std::move(alive), this]() mutable {
            std::shared_ptr<std::atomic<bool>> guard = alive.lock();
            if (!guard || !guard->load())
                return;
            guard->store(false); // Prevent a second deleteLater() from double-deleting.
            delete this;
        });
}

} // namespace Firmir

