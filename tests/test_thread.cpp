#include "test_framework.hpp"

#include <firmir/object.hpp>
#include <firmir/thread.hpp>
#include <firmir/timer.hpp>

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace Firmir;
using namespace std::chrono_literals;

TEST_CASE(signal_direct_connection) {
    Object obj;
    int sum = 0;
    Connection c = obj.connect<int>(&obj, "add", [&](int v) { sum += v; },
                                    ConnectionMode::Direct);
    CHECK(c.valid());
    obj.emitSignal("add", 5);
    obj.emitSignal("add", 7);
    CHECK_EQ(sum, 12);
}

TEST_CASE(signal_disconnect) {
    Object obj;
    int count = 0;
    Connection c = obj.connect(&obj, "sig", [&] { ++count; },
                               ConnectionMode::Direct);
    obj.emitSignal("sig");
    CHECK_EQ(count, 1);
    CHECK(c.disconnect());
    obj.emitSignal("sig");
    CHECK_EQ(count, 1);
    CHECK(!c.disconnect());
}

TEST_CASE(signal_scoped_connection) {
    Object obj;
    int count = 0;
    {
        ScopedConnection sc(obj.connect(&obj, "sig", [&] { ++count; },
                                        ConnectionMode::Direct));
        obj.emitSignal("sig");
        CHECK_EQ(count, 1);
    }
    obj.emitSignal("sig");
    CHECK_EQ(count, 1);
}

TEST_CASE(signal_auto_same_thread_is_direct) {
    Object obj;
    bool direct = false;
    const std::thread::id mainId = std::this_thread::get_id();
    obj.connect(&obj, "sig",
                [&] { direct = std::this_thread::get_id() == mainId; },
                ConnectionMode::Auto);
    obj.emitSignal("sig");
    CHECK(direct);
}

TEST_CASE(signal_queued_cross_thread) {
    Thread worker;
    worker.start();

    Object receiver;
    receiver.moveToThread(&worker);

    std::atomic<int> received{0};
    std::atomic<bool> inWorkerThread{false};
    const std::thread::id mainId = std::this_thread::get_id();

    Object sender;
    sender.connect<int>(&receiver, "data",
                        [&](int v) {
                            received.store(v);
                            inWorkerThread.store(std::this_thread::get_id() != mainId);
                        },
                        ConnectionMode::Auto);

    sender.emitSignal("data", 99);
    CHECK(testing::waitFor([&] { return received.load() == 99; }));
    CHECK(inWorkerThread.load());

    worker.quit();
    CHECK(worker.wait(5s));
    CHECK(worker.isFinished());
}

TEST_CASE(thread_move_invariants) {
    Thread other;
    Object plain;
    plain.moveToThread(&other);
    CHECK(plain.thread() == &other);
    plain.moveToThread(Thread::mainThread());

    Object parentObj;
    Object child(&parentObj);
    CHECK_THROWS_AS(child.moveToThread(&other), std::logic_error);
    CHECK_THROWS_AS(parentObj.moveToThread(&other), std::logic_error);
    CHECK_THROWS_AS(other.moveToThread(&other), std::logic_error);
}

TEST_CASE(thread_exec_quit_main) {
    Object ctx;
    Timer::singleShot(30ms, &ctx,
                      [] { Thread::mainThread()->quit(); });
    const int code = Thread::mainThread()->exec();
    CHECK_EQ(code, 0);
}

TEST_CASE(thread_exec_exit_code) {
    Object ctx;
    Timer::singleShot(20ms, &ctx,
                      [] { Thread::mainThread()->exit(7); });
    const int code = Thread::mainThread()->exec();
    CHECK_EQ(code, 7);
}

namespace {

class IntEvent : public Event {
public:
    explicit IntEvent(int value) : Event(User), value(value) {}
    int value;
};

class Recorder : public Object {
public:
    bool event(Event& e) override {
        if (e.type() == Event::User) {
            record.push_back(static_cast<IntEvent&>(e).value);
            if (record.size() == expected)
                Thread::mainThread()->quit();
            return true;
        }
        return Object::event(e);
    }
    std::vector<int> record;
    std::size_t expected = 0;
};

} // namespace

TEST_CASE(event_post_fifo_order) {
    Recorder recorder;
    recorder.expected = 5;
    for (int i = 1; i <= 5; ++i)
        Object::postEvent(&recorder, std::make_unique<IntEvent>(i));
    Timer::singleShot(2s, &recorder,
                      [] { Thread::mainThread()->exit(9); }); // failsafe
    const int code = Thread::mainThread()->exec();
    CHECK_EQ(code, 0);
    CHECK_EQ(recorder.record.size(), 5u);
    for (int i = 1; i <= 5; ++i)
        CHECK_EQ(recorder.record[static_cast<std::size_t>(i - 1)], i);
}

TEST_CASE(event_filter_interception) {
    Recorder recorder;
    recorder.expected = 1;

    struct Filter : Object {
        bool eventFilter(Object* watched, Event& e) override {
            seen.push_back(static_cast<IntEvent&>(e).value);
            if (e.type() == Event::User && static_cast<IntEvent&>(e).value < 0)
                return true; // consume negatives
            return Object::eventFilter(watched, e);
        }
        std::vector<int> seen;
    } filter;

    recorder.installEventFilter(&filter);
    Object::postEvent(&recorder, std::make_unique<IntEvent>(1));
    Timer::singleShot(2s, &recorder, [] { Thread::mainThread()->exit(9); });
    CHECK_EQ(Thread::mainThread()->exec(), 0);
    CHECK_EQ(filter.seen.size(), 1u);
    CHECK_EQ(recorder.record.size(), 1u);
    recorder.removeEventFilter(&filter);
}

TEST_CASE(delete_later_from_foreign_thread) {
    std::atomic<bool> destroyed{false};
    struct Victim : Object {
        explicit Victim(std::atomic<bool>* f) : flag(f) {}
        ~Victim() override { flag->store(true); }
        std::atomic<bool>* flag;
    };
    auto* victim = new Victim(&destroyed);

    std::thread foreign([&] { victim->deleteLater(); });
    foreign.join();

    Object ctx;
    Timer::singleShot(100ms, &ctx, [] { Thread::mainThread()->quit(); });
    Thread::mainThread()->exec();
    CHECK(destroyed.load());
}

TEST_CASE(stress_many_threads_many_signals) {
    constexpr int kThreads = 4;
    constexpr int kSignals = 250;
    const int total = kThreads * kSignals;

    std::atomic<int> received{0};
    Object receiver; // main thread affinity

    class Emitter : public Object {
    public:
        Object* target = nullptr;
        int count = 0;
        bool event(Event& e) override {
            if (e.type() == Event::User) {
                for (int i = 0; i < count; ++i)
                    emitSignal("data", i);
                return true;
            }
            return Object::event(e);
        }
    };

    std::vector<std::unique_ptr<Thread>> threads;
    std::vector<std::unique_ptr<Emitter>> emitters;
    for (int t = 0; t < kThreads; ++t) {
        threads.push_back(std::make_unique<Thread>());
        auto emitter = std::make_unique<Emitter>();
        emitter->target = &receiver;
        emitter->count = kSignals;
        emitter->connect<int>(&receiver, "data",
                              [&](int) {
                                  if (received.fetch_add(1) + 1 == total)
                                      Thread::mainThread()->quit();
                              },
                              ConnectionMode::Queued);
        emitter->moveToThread(threads.back().get());
        emitters.push_back(std::move(emitter));
    }
    for (auto& t : threads)
        t->start();
    for (auto& e : emitters)
        Object::postEvent(e.get(), std::make_unique<Event>(Event::User));

    Timer::singleShot(20s, &receiver,
                      [] { Thread::mainThread()->exit(3); }); // failsafe
    const int code = Thread::mainThread()->exec();
    CHECK_EQ(code, 0);
    CHECK_EQ(received.load(), total);

    for (auto& t : threads) {
        t->quit();
        CHECK(t->wait(5s));
    }
    // Threads are finished: destroying emitters from main is allowed now.
    emitters.clear();
}

