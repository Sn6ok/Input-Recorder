#include "history/snapshot_policy.hpp"

namespace ir {

bool SnapshotPolicy::on_event(std::int64_t wall_ms) {
    if (!settings_.enabled) return false;

    if (!has_last_) {
        has_last_ = true;
        last_ms_ = wall_ms;
    }
    ++events_since_;

    if (settings_.event_interval > 0 && events_since_ >= settings_.event_interval) {
        return true;
    }
    if (settings_.time_interval_ms > 0 &&
        (wall_ms - last_ms_) >= settings_.time_interval_ms) {
        return true;
    }
    return false;
}

}  // namespace ir
