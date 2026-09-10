#include "framework/test_framework.hpp"

#include <atomic>
#include <thread>
#include <vector>

#include "core/event_queue.hpp"

namespace {

ir::Event make_event(std::uint64_t id) {
    ir::Event e;
    e.id = ir::EventId{id};
    e.type = ir::EventType::KeyDown;
    e.payload = ir::KeyEventData{static_cast<std::uint16_t>(id & 0xFFFF), 0};
    return e;
}

}  // namespace

TEST_CASE("core.queue", "FIFO ordering is preserved") {
    ir::EventQueue q(16);
    for (std::uint64_t i = 1; i <= 10; ++i) {
        CHECK(q.try_push(make_event(i)));
    }
    std::vector<ir::Event> out;
    CHECK_EQ(q.try_drain(out, 100), static_cast<std::size_t>(10));
    for (std::size_t i = 0; i < out.size(); ++i) {
        CHECK_EQ(out[i].id.value, static_cast<std::uint64_t>(i + 1));
    }
}

TEST_CASE("core.queue", "bounded: pushes beyond capacity are dropped, not crashing") {
    ir::EventQueue q(4);
    for (std::uint64_t i = 1; i <= 4; ++i) CHECK(q.try_push(make_event(i)));
    // Next pushes must fail (full), without blocking.
    CHECK(!q.try_push(make_event(5)));
    CHECK(!q.try_push(make_event(6)));

    auto s = q.stats();
    CHECK_EQ(s.pushed, static_cast<std::uint64_t>(4));
    CHECK_EQ(s.dropped, static_cast<std::uint64_t>(2));
    CHECK_EQ(s.size, static_cast<std::size_t>(4));
    CHECK_EQ(s.capacity, static_cast<std::size_t>(4));
}

TEST_CASE("core.queue", "overflow flag is observable and clears on read") {
    ir::EventQueue q(1);
    CHECK(q.try_push(make_event(1)));
    CHECK(!q.try_push(make_event(2)));  // dropped
    CHECK(q.consume_overflow_flag());   // observed
    CHECK(!q.consume_overflow_flag());  // cleared
}

TEST_CASE("core.queue", "ring buffer wraps correctly after draining") {
    ir::EventQueue q(4);
    for (std::uint64_t i = 1; i <= 4; ++i) CHECK(q.try_push(make_event(i)));
    std::vector<ir::Event> out;
    CHECK_EQ(q.try_drain(out, 2), static_cast<std::size_t>(2));  // drain 1,2
    // Now push 5,6 which should occupy the freed slots (wrap-around).
    CHECK(q.try_push(make_event(5)));
    CHECK(q.try_push(make_event(6)));
    out.clear();
    CHECK_EQ(q.try_drain(out, 100), static_cast<std::size_t>(4));  // 3,4,5,6
    CHECK_EQ(out[0].id.value, static_cast<std::uint64_t>(3));
    CHECK_EQ(out[1].id.value, static_cast<std::uint64_t>(4));
    CHECK_EQ(out[2].id.value, static_cast<std::uint64_t>(5));
    CHECK_EQ(out[3].id.value, static_cast<std::uint64_t>(6));
}

TEST_CASE("core.queue", "wait_and_drain returns 0 once closed and empty") {
    ir::EventQueue q(8);
    CHECK(q.try_push(make_event(1)));
    q.close();
    CHECK(!q.try_push(make_event(2)));  // closed: rejected
    std::vector<ir::Event> out;
    CHECK_EQ(q.wait_and_drain(out, 100), static_cast<std::size_t>(1));  // drains leftover
    CHECK_EQ(q.wait_and_drain(out, 100), static_cast<std::size_t>(0));  // now empty+closed
}

TEST_CASE("core.queue", "single producer / single consumer transfers everything") {
    ir::EventQueue q(64);
    constexpr std::uint64_t kCount = 20000;

    std::atomic<std::uint64_t> consumed{0};
    std::uint64_t next_expected = 1;
    bool ordered = true;

    std::thread consumer([&] {
        std::vector<ir::Event> batch;
        for (;;) {
            batch.clear();
            std::size_t n = q.wait_and_drain(batch, 128);
            if (n == 0) break;  // closed + empty
            for (const auto& e : batch) {
                if (e.id.value != next_expected) ordered = false;
                ++next_expected;
                consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    std::uint64_t produced = 0;
    for (std::uint64_t i = 1; i <= kCount; ++i) {
        // Retry on transient fullness so this test asserts no data loss when the
        // consumer keeps up; producer still never blocks.
        while (!q.try_push(make_event(i))) {
            std::this_thread::yield();
        }
        ++produced;
    }
    q.close();
    consumer.join();

    CHECK_EQ(produced, kCount);
    CHECK_EQ(consumed.load(), kCount);
    CHECK(ordered);
}

TEST_CASE("core.queue", "burst from multiple producers loses nothing with retry") {
    ir::EventQueue q(128);
    constexpr int kProducers = 4;
    constexpr std::uint64_t kPerProducer = 5000;

    std::atomic<std::uint64_t> consumed{0};
    std::thread consumer([&] {
        std::vector<ir::Event> batch;
        for (;;) {
            batch.clear();
            std::size_t n = q.wait_and_drain(batch, 256);
            if (n == 0) break;
            consumed.fetch_add(n, std::memory_order_relaxed);
        }
    });

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p) {
        producers.emplace_back([&] {
            for (std::uint64_t i = 0; i < kPerProducer; ++i) {
                while (!q.try_push(make_event(i + 1))) {
                    std::this_thread::yield();
                }
            }
        });
    }
    for (auto& t : producers) t.join();
    q.close();
    consumer.join();

    CHECK_EQ(consumed.load(),
             static_cast<std::uint64_t>(kProducers) * kPerProducer);
}
