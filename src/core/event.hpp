#pragma once

// The central event record. Everything the pipeline captures becomes an Event:
// a stable id, ordering info, a type/category/flags, and a type-specific
// payload (spec §59, §166).

#include <variant>

#include "core/event_data.hpp"
#include "core/event_type.hpp"
#include "core/ids.hpp"
#include "core/time.hpp"

namespace ir {

// std::monostate covers events whose data lives elsewhere (SessionStarted/Ended
// use the session field; ContextChanged uses the context field).
using EventPayload = std::variant<std::monostate, KeyEventData, TextInputData,
                                  ShortcutData, MouseData, ClipboardRef,
                                  DiagnosticData>;

struct Event {
    EventId id;
    SessionId session;
    ContextId context;  // 0 if unknown/not applicable
    Timestamp time;
    EventType type = EventType::Unknown;
    EventCategory category = EventCategory::UserInput;
    EventFlags flags = EventFlags::None;
    EventPayload payload;

    friend bool operator==(const Event& a, const Event& b) {
        return a.id == b.id && a.session == b.session && a.context == b.context &&
               a.time == b.time && a.type == b.type && a.category == b.category &&
               a.flags == b.flags && a.payload == b.payload;
    }
    friend bool operator!=(const Event& a, const Event& b) { return !(a == b); }
};

// Stable ordering: monotonic time first, then id as a tiebreaker (spec §87).
inline bool ordered_before(const Event& a, const Event& b) {
    if (a.time.monotonic_ns != b.time.monotonic_ns) {
        return a.time.monotonic_ns < b.time.monotonic_ns;
    }
    return a.id < b.id;
}

}  // namespace ir
