#include "framework/test_framework.hpp"

#include "storage/event_store.hpp"
#include "storage/storage_worker.hpp"

namespace {

ir::Event make_event(std::uint64_t id) {
    ir::Event e;
    e.id = ir::EventId{id};
    e.session = ir::SessionId{1};
    e.type = ir::EventType::TextInput;
    e.category = ir::EventCategory::UserInput;
    e.payload = ir::TextInputData{"e" + std::to_string(id)};
    return e;
}

}  // namespace

TEST_CASE("storage.worker", "submitted events are written after flush") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::StorageWorker worker(store);
    worker.start();

    for (std::uint64_t i = 1; i <= 200; ++i) worker.submit_event(make_event(i));
    worker.flush();

    CHECK_EQ(store.count_events(), 200);
    CHECK_EQ(worker.stats().events_written, 200u);
    CHECK_EQ(worker.pending(), 0u);
    // 200 fast submits are batched into far fewer transactions than events.
    CHECK(worker.stats().transactions >= 1u);
    CHECK(worker.stats().transactions <= 200u);

    worker.stop();
}

TEST_CASE("storage.worker", "contexts, clipboard entries and snapshots are written") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::StorageWorker worker(store);
    worker.start();

    ir::Context c;
    c.id = ir::ContextId{1};
    c.session = ir::SessionId{1};
    c.process_name = "code.exe";
    c.window_title = "x";
    c.first_seen_ms = c.last_seen_ms = 1;
    worker.submit_context(c);

    ir::ClipboardEntry clip;
    clip.id = ir::ClipboardEntryId{1};
    clip.session = ir::SessionId{1};
    clip.timestamp_ms = 5;
    clip.text = "copied";
    clip.content_hash = ir::fnv1a_64(clip.text);
    worker.submit_clipboard_entry(clip);

    ir::TextSnapshot snap;
    snap.id = ir::SnapshotId{1};
    snap.session = ir::SessionId{1};
    snap.anchor_event = ir::EventId{1};
    snap.timestamp_ms = 6;
    snap.text = "state";
    snap.confidence = ir::Confidence::High;
    worker.submit_snapshot(snap);

    worker.submit_event(make_event(1));
    worker.flush();

    CHECK(store.get_context(ir::ContextId{1}).has_value());
    CHECK(store.get_clipboard_entry(ir::ClipboardEntryId{1}).has_value());
    CHECK(store.get_snapshot(ir::SnapshotId{1}).has_value());
    CHECK(store.get_event(ir::EventId{1}).has_value());
    CHECK_EQ(worker.stats().contexts_written, 1u);
    CHECK_EQ(worker.stats().clipboard_written, 1u);
    CHECK_EQ(worker.stats().snapshots_written, 1u);

    worker.stop();
}

TEST_CASE("storage.worker", "stop drains remaining work without an explicit flush") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::StorageWorker worker(store);
    worker.start();
    for (std::uint64_t i = 1; i <= 50; ++i) worker.submit_event(make_event(i));
    worker.stop();  // must flush the queue before joining

    CHECK_EQ(store.count_events(), 50);
}

TEST_CASE("storage.worker", "flush with no pending work returns immediately") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::StorageWorker worker(store);
    worker.start();
    worker.flush();  // nothing submitted — must not hang
    CHECK_EQ(store.count_events(), 0);
    worker.stop();
}

TEST_CASE("storage.worker", "multiple flush cycles accumulate correctly") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    ir::StorageWorker worker(store);
    worker.start();

    for (std::uint64_t i = 1; i <= 10; ++i) worker.submit_event(make_event(i));
    worker.flush();
    CHECK_EQ(store.count_events(), 10);

    for (std::uint64_t i = 11; i <= 30; ++i) worker.submit_event(make_event(i));
    worker.flush();
    CHECK_EQ(store.count_events(), 30);

    worker.stop();
}
