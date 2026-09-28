#pragma once

// Foreground window/process context tracking with caching and deduplication
// (spec §121, §199, §200, §201).
//
// The tracker turns a stream of foreground-window observations into stable
// Context records with unique ContextIds. Returning to a window/title already
// seen this session reuses its ContextId instead of allocating a new one
// (caching + dedup, spec §200); a title change within the same window instance
// is treated as a distinct context so history preserves what the window said at
// the time (spec §201). Repeated identical observations of the still-active
// window produce no ContextChanged event.
//
// This is OS-free and unit-tested: it consumes WindowObservation, never touches
// <Windows.h>. The Win32 WindowContextMonitor feeds it.

#include <cstdint>
#include <string>
#include <unordered_map>

#include "context/window_observation.hpp"
#include "core/context.hpp"
#include "core/event.hpp"
#include "core/ids.hpp"

namespace ir {

class ContextTracker {
public:
    // `first_id` seeds the ContextId counter so ids stay globally unique across
    // runs (the app passes storage's max context id + 1, spec §445); it defaults
    // to 1 for standalone/tests.
    explicit ContextTracker(SessionId session, std::uint64_t first_id = 1)
        : session_(session), next_id_(first_id), first_id_(first_id) {}

    struct Update {
        bool changed = false;   // the active context changed (emit ContextChanged)
        bool is_new = false;    // a new ContextId was allocated this observation
        Context context;        // the active context record after this observation
    };

    // Feeds one foreground observation and returns the resulting active context.
    Update observe(const WindowObservation& obs);

    ContextId current() const { return current_; }

    // Looks up a stored context by id (nullptr if unknown). Used by storage to
    // persist the Context a ContextChanged event refers to.
    const Context* find(ContextId id) const;

    std::size_t size() const { return by_id_.size(); }

    // Builds a ContextChanged event referencing the active context. The event's
    // id is left at default (assigned downstream); `ts` fills the timestamp.
    Event make_context_changed_event(const Update& u, Timestamp ts) const;

    // Clears all tracked contexts (e.g. on a new session).
    void reset();

private:
    struct Key {
        std::string process_name;
        std::string window_title;
        std::uint32_t process_id = 0;
        std::uint64_t window_handle = 0;
        bool operator==(const Key& other) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& k) const noexcept;
    };

    static Key key_of(const WindowObservation& obs);
    bool matches_current(const Key& key) const;

    SessionId session_;
    std::uint64_t next_id_ = 1;
    std::uint64_t first_id_ = 1;
    ContextId current_{};
    std::unordered_map<ContextId, Context> by_id_;
    std::unordered_map<Key, ContextId, KeyHash> by_key_;
};

}  // namespace ir
