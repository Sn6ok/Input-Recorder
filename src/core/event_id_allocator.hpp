#pragma once

// Monotonic, thread-safe allocator of unique EventIds shared by every capture
// source so ordering (monotonic time + id) is globally consistent (spec §87,
// §445). A 64-bit counter does not realistically overflow (spec §446).

#include <atomic>
#include <cstdint>

#include "core/ids.hpp"

namespace ir {

class EventIdAllocator {
public:
    explicit EventIdAllocator(std::uint64_t start = 1) : next_(start) {}

    EventId next() {
        return EventId{next_.fetch_add(1, std::memory_order_relaxed)};
    }

    std::uint64_t peek() const { return next_.load(std::memory_order_relaxed); }

private:
    std::atomic<std::uint64_t> next_;
};

}  // namespace ir
