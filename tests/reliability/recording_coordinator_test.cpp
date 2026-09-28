#include "framework/test_framework.hpp"

#include "reliability/recording_coordinator.hpp"
#include "reliability/recovery.hpp"
#include "storage/temp_db.hpp"

namespace {

ir::Event text_ev(std::uint64_t id, std::string text) {
    ir::Event e;
    e.id = ir::EventId{id};
    e.session = ir::SessionId{1};
    e.time = ir::Timestamp{static_cast<std::int64_t>(id), static_cast<std::int64_t>(id)};
    e.type = ir::EventType::TextInput;
    e.category = ir::EventCategory::UserInput;
    e.payload = ir::TextInputData{std::move(text)};
    return e;
}

std::int64_t snapshot_count(ir::EventStore& store) {
    ir::SqliteStatement st = store.db().prepare("SELECT COUNT(*) FROM snapshots;");
    st.step();
    return st.column_int64(0);
}

}  // namespace

TEST_CASE("reliability.coordinator", "reconstructs and persists drained events") {
    ir::EventQueue queue(1024);
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::StorageWorker writer(store);
    writer.start();

    ir::RecordingCoordinator coord(queue, writer, store, ir::SessionId{1});
    coord.start();

    CHECK(queue.try_push(text_ev(1, "Hello")));
    CHECK(queue.try_push(text_ev(2, " World")));

    coord.stop();    // close queue, drain, join
    writer.flush();

    CHECK_EQ(store.count_events(), 2);
    CHECK_EQ(coord.view().text, std::string("Hello World"));
    CHECK_EQ(coord.processed(), 2u);
    writer.stop();
}

TEST_CASE("reliability.coordinator", "captures snapshots per the policy") {
    ir::EventQueue queue(1024);
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::StorageWorker writer(store);
    writer.start();

    ir::RecordingCoordinator::Options opts;
    opts.snapshots.event_interval = 2;
    opts.snapshots.time_interval_ms = 0;
    ir::RecordingCoordinator coord(queue, writer, store, ir::SessionId{1}, opts);
    coord.start();

    for (std::uint64_t i = 1; i <= 5; ++i)
        queue.try_push(text_ev(i, std::to_string(i)));

    coord.stop();
    writer.flush();

    CHECK_EQ(coord.snapshots_taken(), 2u);  // after events 2 and 4
    CHECK_EQ(snapshot_count(store), 2);
    writer.stop();
}

TEST_CASE("reliability.coordinator", "degraded mode buffers events for later replay") {
    irtest::TempDbFile tmp;
    ir::EmergencyBuffer buffer;
    REQUIRE(buffer.open(tmp.path));

    ir::EventQueue queue(1024);
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::StorageWorker writer(store);
    writer.start();

    ir::RecordingCoordinator coord(queue, writer, store, ir::SessionId{1}, {},
                                   &buffer);
    coord.set_degraded(true);
    coord.start();

    for (std::uint64_t i = 1; i <= 3; ++i)
        queue.try_push(text_ev(i, "d" + std::to_string(i)));

    coord.stop();
    writer.flush();

    // Nothing reached storage while degraded; reconstruction still worked.
    CHECK_EQ(store.count_events(), 0);
    CHECK_EQ(coord.processed(), 3u);
    CHECK_EQ(buffer.drain().size(), 3u);

    // Recovery replays the buffered events into storage.
    CHECK_EQ(ir::replay_emergency_buffer(buffer, store), 3);
    CHECK_EQ(store.count_events(), 3);
    writer.stop();
}
