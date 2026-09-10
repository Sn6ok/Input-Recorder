#pragma once

// Bounded, thread-safe event queue connecting the capture side (producers) to
// the processing side (consumer: reconstruction + storage). Spec §39, §453.
//
// Design goals:
//   * Producers never block. try_push returns false and counts a drop when the
//     queue is full, so a low-level keyboard hook can enqueue-and-return with no
//     risk of stalling system input (spec §58, §455, §35).
//   * Bounded memory: a fixed-capacity ring buffer, no per-push heap allocation
//     of queue nodes (spec §454, §34).
//   * The consumer drains in batches to keep lock hold time short and to feed
//     batched storage writes (spec §170).
//   * Overflow is observable (dropped count + flag), never hidden (spec §391).

#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "core/event.hpp"

namespace ir {

class EventQueue {
public:
    struct Stats {
        std::uint64_t pushed = 0;      // successfully enqueued
        std::uint64_t dropped = 0;     // rejected because the queue was full
        std::uint64_t high_water = 0;  // max simultaneous size observed
        std::size_t size = 0;          // current size
        std::size_t capacity = 0;
    };

    explicit EventQueue(std::size_t capacity);

    // Producer side, non-blocking. Returns true if enqueued, false if the queue
    // is full (event dropped) or closed.
    bool try_push(Event&& event);

    // Consumer side. Blocks until at least one event is available, or the queue
    // is closed and empty. Moves up to `max_batch` events (FIFO) into `out`
    // (appended). Returns the number moved; 0 means "closed and drained" — the
    // consumer should stop.
    std::size_t wait_and_drain(std::vector<Event>& out, std::size_t max_batch);

    // Non-blocking: move up to `max_batch` currently-available events into
    // `out`. Returns the number moved (may be 0).
    std::size_t try_drain(std::vector<Event>& out, std::size_t max_batch);

    // Marks the queue closed: no further pushes are accepted and any blocked
    // consumer is woken. Already-queued events remain drainable.
    void close();
    bool is_closed() const;

    std::size_t size() const;
    bool empty() const;

    // Snapshot of counters. Also clears the "overflowed since last check" flag.
    Stats stats();

    // Whether any drop occurred since the last call (clears the flag).
    bool consume_overflow_flag();

private:
    std::size_t drain_locked(std::vector<Event>& out, std::size_t max_batch);

    mutable std::mutex mutex_;
    std::condition_variable not_empty_;

    std::vector<Event> buffer_;  // ring storage, fixed size == capacity_
    std::size_t capacity_;
    std::size_t head_ = 0;   // index of the oldest element
    std::size_t count_ = 0;  // number of valid elements

    bool closed_ = false;
    bool overflowed_ = false;

    std::uint64_t pushed_ = 0;
    std::uint64_t dropped_ = 0;
    std::uint64_t high_water_ = 0;
};

}  // namespace ir
