#include "framework/test_framework.hpp"

#include <string>

#include "history/history_service.hpp"
#include "reconstruction/reconstruction_engine.hpp"
#include "storage/event_store.hpp"

namespace {

ir::Event text_ev(std::uint64_t id, ir::SessionId session, std::string text,
                  std::int64_t t = 0) {
    ir::Event e;
    e.id = ir::EventId{id};
    e.session = session;
    e.time = ir::Timestamp{t == 0 ? static_cast<std::int64_t>(id) : t,
                           static_cast<std::int64_t>(id)};
    e.type = ir::EventType::TextInput;
    e.category = ir::EventCategory::UserInput;
    e.payload = ir::TextInputData{std::move(text)};
    return e;
}

ir::Event key_ev(std::uint64_t id, ir::SessionId session, std::uint16_t vk) {
    ir::Event e;
    e.id = ir::EventId{id};
    e.session = session;
    e.time = ir::Timestamp{static_cast<std::int64_t>(id),
                           static_cast<std::int64_t>(id)};
    e.type = ir::EventType::KeyDown;
    e.category = ir::EventCategory::UserInput;
    e.payload = ir::KeyEventData{vk, 0};
    return e;
}

std::int64_t min_event_id(ir::EventStore& store) {
    ir::SqliteStatement st = store.db().prepare("SELECT COALESCE(MIN(id),0) FROM events;");
    st.step();
    return st.column_int64(0);
}

}  // namespace

TEST_CASE("history.service", "make_fts_match quotes tokens safely") {
    CHECK_EQ(ir::make_fts_match("hello world"), std::string("\"hello\" \"world\""));
    CHECK_EQ(ir::make_fts_match("  a-b?c  "), std::string("\"a-b?c\""));
    CHECK_EQ(ir::make_fts_match("say \"hi\""), std::string("\"say\" \"\"\"hi\"\"\""));
    CHECK(ir::make_fts_match("   ").empty());
}

TEST_CASE("history.service", "query_events paginates and orders") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    for (std::uint64_t i = 1; i <= 10; ++i)
        store.insert_event(text_ev(i, ir::SessionId{1}, "t" + std::to_string(i)));
    ir::HistoryService h(store);

    ir::EventQuery q;
    q.session = ir::SessionId{1};
    q.limit = 3;
    q.offset = 0;
    q.newest_first = true;
    auto page = h.query_events(q);
    REQUIRE(page.size() == 3u);
    CHECK_EQ(page[0].id.value, 10u);  // newest first
    CHECK_EQ(page[2].id.value, 8u);

    q.offset = 3;
    auto page2 = h.query_events(q);
    REQUIRE(page2.size() == 3u);
    CHECK_EQ(page2[0].id.value, 7u);

    q.newest_first = false;
    q.offset = 0;
    auto asc = h.query_events(q);
    CHECK_EQ(asc[0].id.value, 1u);

    CHECK_EQ(h.count_events(q), 10);
}

TEST_CASE("history.service", "query_events filters by type and time range") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    store.insert_event(text_ev(1, ir::SessionId{1}, "a", 100));
    store.insert_event(key_ev(2, ir::SessionId{1}, 0x08));
    store.insert_event(text_ev(3, ir::SessionId{1}, "b", 300));
    ir::HistoryService h(store);

    ir::EventQuery q;
    q.type = ir::EventType::TextInput;
    CHECK_EQ(h.count_events(q), 2);

    ir::EventQuery tr;
    tr.since_ms = 200;
    tr.until_ms = 400;
    auto rows = h.query_events(tr);
    REQUIRE(rows.size() == 1u);
    CHECK_EQ(rows[0].id.value, 3u);
}

TEST_CASE("history.service", "full-text search finds events and respects sessions") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    store.insert_event(text_ev(1, ir::SessionId{1}, "hello world"));
    store.insert_event(text_ev(2, ir::SessionId{1}, "goodbye world"));
    store.insert_event(text_ev(3, ir::SessionId{2}, "world domination"));
    ir::HistoryService h(store);

    CHECK_EQ(h.count_search_events("world", std::nullopt), 3);
    CHECK_EQ(h.count_search_events("hello", std::nullopt), 1);
    CHECK_EQ(h.count_search_events("world", ir::SessionId{1}), 2);

    auto hits = h.search_events("goodbye", std::nullopt, 10, 0);
    REQUIRE(hits.size() == 1u);
    CHECK_EQ(hits[0].id.value, 2u);

    // A query full of punctuation/operators must not error.
    CHECK_EQ(h.search_events("(((", std::nullopt, 10, 0).size(), 0u);
    CHECK_EQ(h.search_events("", std::nullopt, 10, 0).size(), 0u);
}

TEST_CASE("history.service", "clipboard listing and search") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    for (std::uint64_t i = 1; i <= 3; ++i) {
        ir::ClipboardEntry e;
        e.id = ir::ClipboardEntryId{i};
        e.session = ir::SessionId{1};
        e.timestamp_ms = static_cast<std::int64_t>(i);
        e.text = (i == 2) ? "secret token phrase" : ("clip " + std::to_string(i));
        e.content_hash = ir::fnv1a_64(e.text);
        store.insert_clipboard_entry(e);
    }
    ir::HistoryService h(store);

    auto list = h.list_clipboard(ir::SessionId{1}, 10, 0, /*newest_first=*/true);
    REQUIRE(list.size() == 3u);
    CHECK_EQ(list[0].id.value, 3u);

    auto found = h.search_clipboard("token", 10, 0);
    REQUIRE(found.size() == 1u);
    CHECK_EQ(found[0].id.value, 2u);
}

TEST_CASE("history.service", "sessions are listed newest-first") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    for (std::uint64_t i = 1; i <= 3; ++i) {
        ir::Session s;
        s.id = ir::SessionId{i};
        s.started_at_ms = static_cast<std::int64_t>(i * 10);
        store.insert_session(s);
    }
    ir::HistoryService h(store);
    auto sessions = h.list_sessions();
    REQUIRE(sessions.size() == 3u);
    CHECK_EQ(sessions[0].id.value, 3u);
}

TEST_CASE("history.service", "reconstruct_full replays a session's text") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    const ir::SessionId sid{1};
    store.insert_event(text_ev(1, sid, "Hello"));
    store.insert_event(text_ev(2, sid, " World"));
    store.insert_event(key_ev(3, sid, 0x08));  // Backspace
    ir::HistoryService h(store);

    auto r = h.reconstruct_full(sid, ir::EventId{3});
    CHECK_EQ(r.text, std::string("Hello Worl"));
    CHECK(r.confidence == ir::Confidence::High);
}

TEST_CASE("history.service", "reconstruct_at via snapshot equals a full replay") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    const ir::SessionId sid{7};
    store.insert_event(text_ev(1, sid, "The quick "));
    store.insert_event(text_ev(2, sid, "brown fox"));
    store.insert_event(key_ev(3, sid, 0x08));   // Backspace -> "The quick brown fo"
    store.insert_event(text_ev(4, sid, "x!"));  // -> "The quick brown fox!"
    store.insert_event(text_ev(5, sid, "?"));

    ir::HistoryService h(store);
    const ir::EventId target{5};
    ir::Reconstruction full = h.reconstruct_full(sid, target);

    // Build an exact snapshot at event 3 from a direct engine replay.
    ir::ReconstructionEngine eng;
    eng.process(text_ev(1, sid, "The quick "));
    eng.process(text_ev(2, sid, "brown fox"));
    eng.process(key_ev(3, sid, 0x08));
    ir::TextSnapshot snap;
    snap.id = ir::SnapshotId{1};
    snap.session = sid;
    snap.anchor_event = ir::EventId{3};
    snap.timestamp_ms = 3;
    snap.text = eng.text();
    snap.confidence = eng.confidence();
    snap.cursor = eng.cursor();
    snap.cursor_known = eng.cursor_known();
    REQUIRE(store.insert_snapshot(snap));

    ir::Reconstruction viaSnap = h.reconstruct_at(sid, target);
    CHECK_EQ(viaSnap.text, full.text);
    CHECK(viaSnap.confidence == full.confidence);
    CHECK_EQ(viaSnap.cursor, full.cursor);
    CHECK(viaSnap.cursor_known == full.cursor_known);

    // Sanity: the snapshot really was used (it exists at/ before the target).
    auto latest = h.latest_snapshot_at_or_before(sid, target);
    REQUIRE(latest.has_value());
    CHECK_EQ(latest->anchor_event.value, 3u);
}

TEST_CASE("history.service", "purge_before removes old data and updates FTS") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    const ir::SessionId sid{1};
    store.insert_event(text_ev(1, sid, "old apple", 100));
    store.insert_event(text_ev(2, sid, "new banana", 1000));
    ir::HistoryService h(store);

    CHECK_EQ(h.count_search_events("apple", std::nullopt), 1);
    auto r = h.purge_before(500);
    CHECK_EQ(r.events, 1);
    CHECK_EQ(store.count_events(), 1);
    // FTS index no longer returns the purged event.
    CHECK_EQ(h.count_search_events("apple", std::nullopt), 0);
    CHECK_EQ(h.count_search_events("banana", std::nullopt), 1);
}

TEST_CASE("history.service", "apply_retention keeps everything when days<=0") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    store.insert_event(text_ev(1, ir::SessionId{1}, "x", 1));
    ir::HistoryService h(store);
    auto r = h.apply_retention(1'000'000'000, 0);
    CHECK_EQ(r.total(), 0);
    CHECK_EQ(store.count_events(), 1);
}

TEST_CASE("history.service", "clear_all empties the history tables") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    store.insert_event(text_ev(1, ir::SessionId{1}, "a"));
    ir::ClipboardEntry ce;
    ce.id = ir::ClipboardEntryId{1};
    ce.session = ir::SessionId{1};
    ce.text = "c";
    ce.content_hash = ir::fnv1a_64(ce.text);
    store.insert_clipboard_entry(ce);
    ir::HistoryService h(store);

    CHECK(h.clear_all());
    CHECK_EQ(store.count_events(), 0);
    CHECK_EQ(h.list_clipboard(std::nullopt, 10, 0).size(), 0u);
}

TEST_CASE("history.service", "enforce_size_cap trims oldest events to fit") {
    ir::EventStore store;
    REQUIRE(store.open_memory());
    const ir::SessionId sid{1};
    // Grow the database with many sizeable text events.
    std::string big(240, 'z');
    for (std::uint64_t i = 1; i <= 3000; ++i)
        store.insert_event(text_ev(i, sid, big + std::to_string(i)));

    ir::HistoryService h(store);
    const std::int64_t size_before = h.database_size_bytes();
    const std::int64_t count_before = store.count_events();
    REQUIRE(count_before == 3000);
    REQUIRE(size_before > 0);

    const auto cap = static_cast<std::uint64_t>(size_before / 2);
    auto r = h.enforce_size_cap(cap);

    CHECK(r.events > 0);
    CHECK(store.count_events() < count_before);
    CHECK(h.database_size_bytes() <= size_before);
    CHECK(h.database_size_bytes() <= static_cast<std::int64_t>(cap));
    CHECK(min_event_id(store) > 1);  // oldest were trimmed first
}
