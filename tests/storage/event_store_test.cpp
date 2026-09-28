#include "framework/test_framework.hpp"

#include <string>

#include "storage/event_store.hpp"
#include "storage/temp_db.hpp"

namespace {

ir::Event key_event(std::uint64_t id, ir::SessionId session, ir::ContextId ctx) {
    ir::Event e;
    e.id = ir::EventId{id};
    e.session = session;
    e.context = ctx;
    e.time = ir::Timestamp{1000 + static_cast<std::int64_t>(id), 2000 + static_cast<std::int64_t>(id)};
    e.type = ir::EventType::KeyDown;
    e.category = ir::EventCategory::UserInput;
    e.flags = ir::EventFlags::ModCtrl;
    e.payload = ir::KeyEventData{0x41, 0x1E};
    return e;
}

ir::Event text_event(std::uint64_t id, std::string text) {
    ir::Event e;
    e.id = ir::EventId{id};
    e.session = ir::SessionId{1};
    e.type = ir::EventType::TextInput;
    e.category = ir::EventCategory::UserInput;
    e.payload = ir::TextInputData{std::move(text)};
    return e;
}

}  // namespace

TEST_CASE("storage.store", "schema initializes with the expected version") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    CHECK_EQ(store.schema_version(), ir::kStorageSchemaVersion);
}

TEST_CASE("storage.store", "session insert, finalize and read back") {
    ir::EventStore store;
    REQUIRE(store.open_memory());

    ir::Session s;
    s.id = ir::SessionId{5};
    s.started_at_ms = 111;
    s.status = ir::SessionStatus::Active;
    s.app_version = "0.1.0";
    CHECK(store.insert_session(s));

    auto got = store.get_session(ir::SessionId{5});
    REQUIRE(got.has_value());
    CHECK(!got->ended_at_ms.has_value());
    CHECK_EQ(got->app_version, std::string("0.1.0"));

    CHECK(store.finalize_session(ir::SessionId{5}, 222,
                                 ir::SessionStatus::Completed));
    auto done = store.get_session(ir::SessionId{5});
    REQUIRE(done.has_value());
    REQUIRE(done->ended_at_ms.has_value());
    CHECK_EQ(*done->ended_at_ms, 222);
    CHECK(done->status == ir::SessionStatus::Completed);
}

TEST_CASE("storage.store", "context upsert updates title and last-seen") {
    ir::EventStore store;
    REQUIRE(store.open_memory());

    ir::Context c;
    c.id = ir::ContextId{3};
    c.session = ir::SessionId{1};
    c.process_name = "code.exe";
    c.window_title = "a.cpp";
    c.process_id = 100;
    c.window_handle = 0xAB;
    c.first_seen_ms = 10;
    c.last_seen_ms = 10;
    CHECK(store.upsert_context(c));

    c.window_title = "b.cpp";
    c.last_seen_ms = 50;
    CHECK(store.upsert_context(c));

    auto got = store.get_context(ir::ContextId{3});
    REQUIRE(got.has_value());
    CHECK_EQ(got->window_title, std::string("b.cpp"));
    CHECK_EQ(got->first_seen_ms, 10);  // preserved
    CHECK_EQ(got->last_seen_ms, 50);   // updated
}

TEST_CASE("storage.store", "clipboard entry round-trips") {
    ir::EventStore store;
    REQUIRE(store.open_memory());

    ir::ClipboardEntry e;
    e.id = ir::ClipboardEntryId{8};
    e.session = ir::SessionId{1};
    e.source_context = ir::ContextId{2};
    e.timestamp_ms = 999;
    e.text = "clip text \xF0\x9F\x98\x80";  // includes an emoji (4-byte UTF-8)
    e.original_size_bytes = 1234;
    e.content_hash = ir::fnv1a_64(e.text);
    e.truncated = true;
    CHECK(store.insert_clipboard_entry(e));

    auto got = store.get_clipboard_entry(ir::ClipboardEntryId{8});
    REQUIRE(got.has_value());
    CHECK(*got == e);
}

TEST_CASE("storage.store", "events round-trip with payload and typed fields") {
    ir::EventStore store;
    REQUIRE(store.open_memory());

    ir::Event k = key_event(1, ir::SessionId{2}, ir::ContextId{4});
    ir::Event t = text_event(2, "hello world");
    CHECK(store.insert_event(k));
    CHECK(store.insert_event(t));

    auto gk = store.get_event(ir::EventId{1});
    REQUIRE(gk.has_value());
    CHECK(*gk == k);

    auto gt = store.get_event(ir::EventId{2});
    REQUIRE(gt.has_value());
    CHECK(*gt == t);

    CHECK_EQ(store.count_events(), 2);
    CHECK_EQ(store.count_events_in_session(ir::SessionId{2}), 1);
}

TEST_CASE("storage.store", "searchable text is stored for text and shortcut events") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    store.insert_event(text_event(1, "findable phrase"));

    ir::Event shortcut;
    shortcut.id = ir::EventId{2};
    shortcut.session = ir::SessionId{1};
    shortcut.type = ir::EventType::KeyboardShortcut;
    shortcut.payload = ir::ShortcutData{0, 0x43, "Ctrl+C"};
    store.insert_event(shortcut);

    ir::Event key = key_event(3, ir::SessionId{1}, ir::ContextId{0});
    store.insert_event(key);

    // The text column is populated only for user-text-bearing events.
    ir::SqliteStatement st =
        store.db().prepare("SELECT id FROM events WHERE text IS NOT NULL ORDER BY id;");
    REQUIRE(st.valid());
    REQUIRE(st.step() == ir::SqliteStatement::Step::Row);
    CHECK_EQ(st.column_int64(0), 1);
    REQUIRE(st.step() == ir::SqliteStatement::Step::Row);
    CHECK_EQ(st.column_int64(0), 2);
    CHECK(st.step() == ir::SqliteStatement::Step::Done);  // key event has no text
}

TEST_CASE("storage.store", "searchable_text extracts the right field") {
    CHECK(ir::searchable_text(text_event(1, "abc")).value() == "abc");
    ir::Event sc;
    sc.type = ir::EventType::KeyboardShortcut;
    sc.payload = ir::ShortcutData{0, 0x43, "Ctrl+C"};
    CHECK(ir::searchable_text(sc).value() == "Ctrl+C");
    CHECK(!ir::searchable_text(key_event(1, ir::SessionId{1}, ir::ContextId{0}))
               .has_value());
}

TEST_CASE("storage.store", "snapshot round-trips") {
    ir::EventStore store;
    REQUIRE(store.open_memory());

    ir::TextSnapshot s;
    s.id = ir::SnapshotId{4};
    s.session = ir::SessionId{1};
    s.context = ir::ContextId{2};
    s.anchor_event = ir::EventId{100};
    s.timestamp_ms = 777;
    s.text = "reconstructed";
    s.confidence = ir::Confidence::Medium;
    CHECK(store.insert_snapshot(s));

    auto got = store.get_snapshot(ir::SnapshotId{4});
    REQUIRE(got.has_value());
    CHECK(*got == s);
}

TEST_CASE("storage.store", "settings round-trip") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    std::map<std::string, std::string> kv{
        {"theme", "Dark"}, {"retention_days", "90"}, {"enable_recording", "true"}};
    CHECK(store.save_settings(kv));
    // Overwrite one key to confirm upsert semantics.
    CHECK(store.save_settings({{"theme", "Light"}}));

    auto loaded = store.load_settings();
    CHECK_EQ(loaded["theme"], std::string("Light"));
    CHECK_EQ(loaded["retention_days"], std::string("90"));
    CHECK_EQ(loaded["enable_recording"], std::string("true"));
}

TEST_CASE("storage.store", "max id queries support allocator seeding") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    CHECK_EQ(store.max_event_id(), 0u);
    store.insert_event(key_event(10, ir::SessionId{1}, ir::ContextId{0}));
    store.insert_event(key_event(25, ir::SessionId{1}, ir::ContextId{0}));
    CHECK_EQ(store.max_event_id(), 25u);
}

TEST_CASE("storage.store", "file database enables WAL and persists across reopen") {
    irtest::TempDbFile tmp;
    {
        ir::EventStore store;
        REQUIRE(store.open(tmp.path));
        // WAL is only meaningful for a file-backed database.
        ir::SqliteStatement st = store.db().prepare("PRAGMA journal_mode;");
        REQUIRE(st.step() == ir::SqliteStatement::Step::Row);
        CHECK_EQ(st.column_text(0), std::string("wal"));
        store.insert_event(text_event(1, "persisted"));
    }
    {
        ir::EventStore store;
        REQUIRE(store.open(tmp.path));
        auto got = store.get_event(ir::EventId{1});
        REQUIRE(got.has_value());
        CHECK(std::get<ir::TextInputData>(got->payload).text == "persisted");
    }
}
