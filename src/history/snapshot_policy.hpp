#pragma once

// Decides when the reconstruction pipeline should persist a text snapshot
// (spec §76, §181, §257). A snapshot is due after a configurable number of
// recorded events, or after a configurable amount of time since the last
// snapshot — whichever comes first — so restore never has to replay a whole
// long session. OS-free and unit-tested.

#include <cstdint>

namespace ir {

struct SnapshotSettings {
    bool enabled = true;
    std::int32_t event_interval = 200;       // snapshot every N recorded events
    std::int32_t time_interval_ms = 30000;   // ...or every N ms, whichever first
};

class SnapshotPolicy {
public:
    SnapshotPolicy() = default;
    explicit SnapshotPolicy(const SnapshotSettings& s) : settings_(s) {}

    // Call once per recorded event. Returns true when a snapshot is due; the
    // caller then captures one and calls note_snapshot().
    bool on_event(std::int64_t wall_ms);

    // Record that a snapshot was just taken, resetting the counters.
    void note_snapshot(std::int64_t wall_ms) {
        events_since_ = 0;
        last_ms_ = wall_ms;
        has_last_ = true;
    }

    void set_settings(const SnapshotSettings& s) { settings_ = s; }
    const SnapshotSettings& settings() const { return settings_; }

    void reset() {
        events_since_ = 0;
        has_last_ = false;
        last_ms_ = 0;
    }

private:
    SnapshotSettings settings_{};
    std::int32_t events_since_ = 0;
    std::int64_t last_ms_ = 0;
    bool has_last_ = false;
};

}  // namespace ir
