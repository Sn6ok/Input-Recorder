#include "reliability/recovery.hpp"

#include <vector>

#include "core/session.hpp"

namespace ir {

int mark_interrupted_sessions(EventStore& store) {
    SqliteStatement st = store.db().prepare(
        "UPDATE sessions SET status=?1 WHERE status=?2;");
    if (!st.valid()) return 0;
    st.bind_int64(1, static_cast<std::int64_t>(SessionStatus::Interrupted));
    st.bind_int64(2, static_cast<std::int64_t>(SessionStatus::Active));
    if (st.step() != SqliteStatement::Step::Done) return 0;
    return store.db().changes();
}

int replay_emergency_buffer(EmergencyBuffer& buffer, EventStore& store) {
    std::vector<Event> events = buffer.drain();
    if (events.empty()) {
        buffer.clear();
        return 0;
    }
    int written = 0;
    const bool ok = store.db().transaction([&] {
        for (const Event& e : events) {
            if (store.insert_event(e)) ++written;
        }
        return true;
    });
    if (ok) buffer.clear();
    return written;
}

}  // namespace ir
