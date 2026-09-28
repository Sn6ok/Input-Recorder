#include "framework/test_framework.hpp"

#include <string>

#include "core/event_queue.hpp"
#include "history/history_service.hpp"
#include "reliability/recording_coordinator.hpp"
#include "storage/event_store.hpp"
#include "storage/storage_worker.hpp"

// End-to-end test of the OS-independent pipeline: a realistic event stream is
// pushed through the queue, processed by the coordinator (reconstruction +
// async storage + snapshots), then queried back through HistoryService. This is
// the "everything wired together" QA check that the individual unit tests do not
// exercise as a whole.

namespace {

std::uint64_t g_id = 0;
std::int64_t g_t = 0;

ir::Event next_base(ir::EventType type, ir::ContextId ctx) {
    ir::Event e;
    e.id = ir::EventId{++g_id};
    e.session = ir::SessionId{1};
    e.context = ctx;
    ++g_t;
    e.time = ir::Timestamp{g_t, g_t};
    e.type = type;
    e.category = ir::EventCategory::UserInput;
    return e;
}

ir::Event text(const std::string& s, ir::ContextId ctx) {
    ir::Event e = next_base(ir::EventType::TextInput, ctx);
    e.payload = ir::TextInputData{s};
    return e;
}

ir::Event key(std::uint16_t vk, ir::ContextId ctx) {
    ir::Event e = next_base(ir::EventType::KeyDown, ctx);
    e.payload = ir::KeyEventData{vk, 0};
    return e;
}

ir::Event paste(const std::string& s, ir::ContextId ctx) {
    ir::Event e = next_base(ir::EventType::Paste, ctx);
    e.payload = ir::TextInputData{s};
    return e;
}

}  // namespace

TEST_CASE("integration", "types, edits and pastes reconstruct end to end") {
    g_id = 0;
    g_t = 0;
    const ir::ContextId ctx{1};

    ir::EventQueue queue(8192);
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::StorageWorker writer(store);
    writer.start();

    ir::RecordingCoordinator::Options opts;
    opts.snapshots.event_interval = 4;
    opts.snapshots.time_interval_ms = 0;
    ir::RecordingCoordinator coord(queue, writer, store, ir::SessionId{1}, opts);
    coord.start();

    // "Hello " + "World" -> "Hello World"; Backspace -> "Hello Worl";
    // "d" -> "Hello World"; paste "! Bye" -> "Hello World! Bye".
    queue.try_push(text("Hello ", ctx));
    queue.try_push(text("World", ctx));
    queue.try_push(key(0x08, ctx));  // Backspace
    queue.try_push(text("d", ctx));
    queue.try_push(paste("! Bye", ctx));

    coord.stop();
    writer.flush();

    const std::string expected = "Hello World! Bye";
    CHECK_EQ(coord.view().text, expected);

    ir::HistoryService h(store);
    // Stored events replay to the same text.
    auto r = h.reconstruct_full(ir::SessionId{1}, ir::EventId{g_id});
    CHECK_EQ(r.text, expected);

    // Full-text search finds typed text and pasted text.
    CHECK(h.count_search_events("World", std::nullopt) >= 1);
    CHECK(h.count_search_events("Bye", std::nullopt) >= 1);

    // Snapshots were captured along the way.
    ir::SqliteStatement st = store.db().prepare("SELECT COUNT(*) FROM snapshots;");
    st.step();
    CHECK(st.column_int64(0) >= 1);

    writer.stop();
}

TEST_CASE("integration", "retention purges old sessions and keeps recent ones") {
    ir::EventStore store;
    REQUIRE(store.open_memory());

    // Two events far apart in time.
    ir::Event old_e;
    old_e.id = ir::EventId{1};
    old_e.session = ir::SessionId{1};
    old_e.type = ir::EventType::TextInput;
    old_e.time.wall_ms = 1'000;
    old_e.payload = ir::TextInputData{"ancient history"};
    store.insert_event(old_e);

    ir::Event new_e;
    new_e.id = ir::EventId{2};
    new_e.session = ir::SessionId{1};
    new_e.type = ir::EventType::TextInput;
    new_e.time.wall_ms = 10'000'000'000;  // much later
    new_e.payload = ir::TextInputData{"recent note"};
    store.insert_event(new_e);

    ir::HistoryService h(store);
    CHECK_EQ(store.count_events(), 2);
    auto purged = h.purge_before(5'000);  // drop the ancient one
    CHECK_EQ(purged.events, 1);
    CHECK_EQ(store.count_events(), 1);
    CHECK_EQ(h.count_search_events("ancient", std::nullopt), 0);
    CHECK_EQ(h.count_search_events("recent", std::nullopt), 1);
}
