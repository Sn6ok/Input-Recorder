#include "context/context_tracker.hpp"

namespace ir {

std::size_t ContextTracker::KeyHash::operator()(const Key& k) const noexcept {
    // FNV-1a-ish combine over the user-meaningful fields plus the run-local ids.
    std::size_t h = 1469598103934665603ull;
    auto mix = [&h](std::size_t v) {
        h ^= v;
        h *= 1099511628211ull;
    };
    mix(std::hash<std::string>{}(k.process_name));
    mix(std::hash<std::string>{}(k.window_title));
    mix(std::hash<std::uint32_t>{}(k.process_id));
    mix(std::hash<std::uint64_t>{}(k.window_handle));
    return h;
}

ContextTracker::Key ContextTracker::key_of(const WindowObservation& obs) {
    return Key{obs.process_name, obs.window_title, obs.process_id,
               obs.window_handle};
}

bool ContextTracker::matches_current(const Key& key) const {
    if (!current_.valid()) return false;
    auto it = by_id_.find(current_);
    if (it == by_id_.end()) return false;
    const Context& c = it->second;
    return c.process_name == key.process_name &&
           c.window_title == key.window_title &&
           c.process_id == key.process_id && c.window_handle == key.window_handle;
}

ContextTracker::Update ContextTracker::observe(const WindowObservation& obs) {
    const Key key = key_of(obs);

    // Unchanged active window: just refresh last-seen, no event.
    if (matches_current(key)) {
        Context& c = by_id_[current_];
        c.last_seen_ms = obs.wall_ms;
        return Update{false, false, c};
    }

    ContextId id;
    bool is_new = false;
    if (auto it = by_key_.find(key); it != by_key_.end()) {
        id = it->second;  // returning to a previously-seen window/title (dedup)
        Context& c = by_id_[id];
        c.last_seen_ms = obs.wall_ms;
    } else {
        id = ContextId{next_id_++};
        is_new = true;
        Context c;
        c.id = id;
        c.session = session_;
        c.process_name = obs.process_name;
        c.window_title = obs.window_title;
        c.process_id = obs.process_id;
        c.window_handle = obs.window_handle;
        c.first_seen_ms = obs.wall_ms;
        c.last_seen_ms = obs.wall_ms;
        by_id_[id] = std::move(c);
        by_key_[key] = id;
    }

    current_ = id;
    return Update{true, is_new, by_id_[id]};
}

const Context* ContextTracker::find(ContextId id) const {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &it->second;
}

Event ContextTracker::make_context_changed_event(const Update& u,
                                                 Timestamp ts) const {
    Event e;
    e.session = session_;
    e.context = u.context.id;
    e.type = EventType::ContextChanged;
    e.category = EventCategory::ContextEvent;
    e.time = ts;
    e.payload = std::monostate{};
    return e;
}

void ContextTracker::reset() {
    by_id_.clear();
    by_key_.clear();
    current_ = ContextId{};
    next_id_ = 1;
}

}  // namespace ir
