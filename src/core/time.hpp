#pragma once

// Event timing. Each event carries both a wall-clock time (for display) and a
// monotonic time (for stable ordering that survives system-clock changes) —
// spec §86, §442.

#include <cstdint>
#include <string>

namespace ir {

struct Timestamp {
    // Milliseconds since the Unix epoch (UTC). Used for display.
    std::int64_t wall_ms = 0;
    // Nanoseconds from a monotonic clock. Comparable only within one process
    // run; used together with EventId for ordering (spec §87).
    std::int64_t monotonic_ns = 0;

    friend bool operator==(const Timestamp& a, const Timestamp& b) {
        return a.wall_ms == b.wall_ms && a.monotonic_ns == b.monotonic_ns;
    }
    friend bool operator!=(const Timestamp& a, const Timestamp& b) { return !(a == b); }
};

// Formats wall_ms as "YYYY-MM-DD HH:MM:SS.mmm" in UTC (deterministic; used for
// logs/tests). Local-time presentation is a UI concern.
std::string format_utc(std::int64_t wall_ms);

// Produces timestamps from the real clocks. Kept as a tiny interface-free class
// so the rest of core has no direct dependency on <chrono> call sites; tests
// can also construct Timestamps directly.
class Clock {
public:
    // Current wall-clock time in ms since the Unix epoch (UTC).
    static std::int64_t now_wall_ms();
    // Current monotonic time in ns.
    static std::int64_t now_monotonic_ns();
    // Convenience: a fully populated Timestamp from both clocks.
    static Timestamp now();
};

}  // namespace ir
