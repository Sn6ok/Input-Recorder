#include "framework/test_framework.hpp"

#include "reliability/recovery.hpp"
#include "storage/temp_db.hpp"

namespace {

ir::Session active_session(std::uint64_t id) {
    ir::Session s;
    s.id = ir::SessionId{id};
    s.started_at_ms = static_cast<std::int64_t>(id);
    s.status = ir::SessionStatus::Active;
    return s;
}

ir::Event ev(std::uint64_t id) {
    ir::Event e;
    e.id = ir::EventId{id};
    e.session = ir::SessionId{1};
    e.type = ir::EventType::TextInput;
    e.payload = ir::TextInputData{"e" + std::to_string(id)};
    return e;
}

}  // namespace

TEST_CASE("reliability.recovery", "active sessions are flagged interrupted") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    store.insert_session(active_session(1));
    store.insert_session(active_session(2));
    ir::Session done;
    done.id = ir::SessionId{3};
    done.status = ir::SessionStatus::Completed;
    store.insert_session(done);

    CHECK_EQ(ir::mark_interrupted_sessions(store), 2);
    CHECK(store.get_session(ir::SessionId{1})->status ==
          ir::SessionStatus::Interrupted);
    CHECK(store.get_session(ir::SessionId{3})->status ==
          ir::SessionStatus::Completed);  // untouched

    // Idempotent: a second pass finds nothing to change.
    CHECK_EQ(ir::mark_interrupted_sessions(store), 0);
}

TEST_CASE("reliability.recovery", "emergency buffer replays into storage and clears") {
    irtest::TempDbFile tmp;
    ir::EmergencyBuffer buf;
    REQUIRE(buf.open(tmp.path));
    buf.append(ev(1));
    buf.append(ev(2));

    ir::EventStore store;
    REQUIRE(store.open_memory());
    CHECK_EQ(ir::replay_emergency_buffer(buf, store), 2);
    CHECK_EQ(store.count_events(), 2);
    CHECK(store.get_event(ir::EventId{2}).has_value());
    // Buffer is emptied after a successful replay.
    CHECK_EQ(buf.drain().size(), 0u);
}
