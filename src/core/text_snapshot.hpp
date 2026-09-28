#pragma once

// A reconstructed-text snapshot anchored to a point in the event stream
// (spec §76, §181). Snapshot + events-after-snapshot lets the engine restore
// state without replaying an entire session.

#include <cstdint>
#include <string>

#include "core/confidence.hpp"
#include "core/ids.hpp"

namespace ir {

struct TextSnapshot {
    SnapshotId id;
    SessionId session;
    ContextId context;
    EventId anchor_event;       // last event reflected in `text`
    std::int64_t timestamp_ms = 0;
    std::string text;           // reconstructed text, UTF-8
    Confidence confidence = Confidence::High;
    // Caret state at the snapshot point, so a restore can fast-forward from here
    // and keep replaying subsequent edits exactly (spec §76, §211). Measured in
    // Unicode code points into `text`.
    std::uint64_t cursor = 0;
    bool cursor_known = true;

    friend bool operator==(const TextSnapshot& a, const TextSnapshot& b) {
        return a.id == b.id && a.session == b.session && a.context == b.context &&
               a.anchor_event == b.anchor_event && a.timestamp_ms == b.timestamp_ms &&
               a.text == b.text && a.confidence == b.confidence &&
               a.cursor == b.cursor && a.cursor_known == b.cursor_known;
    }
};

}  // namespace ir
