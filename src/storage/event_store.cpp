#include "storage/event_store.hpp"

#include <cstdlib>
#include <string>
#include <variant>

#include "core/serialization.hpp"

namespace ir {
namespace {

constexpr const char* kSchemaSql = R"sql(
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS settings (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions (
    id          INTEGER PRIMARY KEY,
    started_ms  INTEGER NOT NULL,
    ended_ms    INTEGER,
    status      INTEGER NOT NULL,
    app_version TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS contexts (
    id            INTEGER PRIMARY KEY,
    session_id    INTEGER NOT NULL,
    process_name  TEXT NOT NULL,
    window_title  TEXT NOT NULL,
    process_id    INTEGER NOT NULL,
    window_handle INTEGER NOT NULL,
    first_seen_ms INTEGER NOT NULL,
    last_seen_ms  INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS clipboard_entries (
    id                  INTEGER PRIMARY KEY,
    session_id          INTEGER NOT NULL,
    source_context      INTEGER NOT NULL DEFAULT 0,
    timestamp_ms        INTEGER NOT NULL,
    text                TEXT NOT NULL,
    original_size_bytes INTEGER NOT NULL,
    content_hash        INTEGER NOT NULL,
    truncated           INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS events (
    id           INTEGER PRIMARY KEY,
    session_id   INTEGER NOT NULL,
    context_id   INTEGER NOT NULL DEFAULT 0,
    wall_ms      INTEGER NOT NULL,
    monotonic_ns INTEGER NOT NULL,
    type         INTEGER NOT NULL,
    category     INTEGER NOT NULL,
    flags        INTEGER NOT NULL,
    text         TEXT,
    payload      BLOB
);
CREATE TABLE IF NOT EXISTS snapshots (
    id           INTEGER PRIMARY KEY,
    session_id   INTEGER NOT NULL,
    context_id   INTEGER NOT NULL DEFAULT 0,
    anchor_event INTEGER NOT NULL,
    timestamp_ms INTEGER NOT NULL,
    text         TEXT NOT NULL,
    confidence   INTEGER NOT NULL,
    cursor       INTEGER NOT NULL DEFAULT 0,
    cursor_known INTEGER NOT NULL DEFAULT 1
);
CREATE INDEX IF NOT EXISTS idx_events_session ON events(session_id);
CREATE INDEX IF NOT EXISTS idx_events_wall    ON events(wall_ms);
CREATE INDEX IF NOT EXISTS idx_events_type    ON events(type);
CREATE INDEX IF NOT EXISTS idx_ctx_session    ON contexts(session_id);
CREATE INDEX IF NOT EXISTS idx_clip_session   ON clipboard_entries(session_id);
CREATE INDEX IF NOT EXISTS idx_snap_session   ON snapshots(session_id);

-- Full-text search over event text (TextInput / Paste / shortcut labels) and
-- clipboard text (spec §187). External-content FTS5 kept in sync by triggers;
-- event/clipboard text is immutable so only insert/delete triggers are needed.
CREATE VIRTUAL TABLE IF NOT EXISTS events_fts USING fts5(
    text, content='events', content_rowid='id', tokenize='unicode61');
CREATE TRIGGER IF NOT EXISTS events_fts_ai AFTER INSERT ON events
    WHEN new.text IS NOT NULL BEGIN
    INSERT INTO events_fts(rowid, text) VALUES (new.id, new.text);
END;
CREATE TRIGGER IF NOT EXISTS events_fts_ad AFTER DELETE ON events
    WHEN old.text IS NOT NULL BEGIN
    INSERT INTO events_fts(events_fts, rowid, text) VALUES('delete', old.id, old.text);
END;

CREATE VIRTUAL TABLE IF NOT EXISTS clip_fts USING fts5(
    text, content='clipboard_entries', content_rowid='id', tokenize='unicode61');
CREATE TRIGGER IF NOT EXISTS clip_fts_ai AFTER INSERT ON clipboard_entries BEGIN
    INSERT INTO clip_fts(rowid, text) VALUES (new.id, new.text);
END;
CREATE TRIGGER IF NOT EXISTS clip_fts_ad AFTER DELETE ON clipboard_entries BEGIN
    INSERT INTO clip_fts(clip_fts, rowid, text) VALUES('delete', old.id, old.text);
END;
)sql";

}  // namespace

std::optional<std::string> searchable_text(const Event& e) {
    if (const auto* t = std::get_if<TextInputData>(&e.payload)) {
        if (!t->text.empty()) return t->text;
        return std::nullopt;
    }
    if (const auto* s = std::get_if<ShortcutData>(&e.payload)) {
        if (!s->text.empty()) return s->text;
        return std::nullopt;
    }
    return std::nullopt;
}

bool EventStore::apply_pragmas() {
    // WAL keeps readers (UI) from blocking the writer and survives crashes
    // better (spec §172, §176). synchronous=NORMAL is the recommended, durable-
    // enough companion to WAL. A busy timeout avoids spurious SQLITE_BUSY.
    // auto_vacuum must be set before any table is created (fresh database), so
    // it comes first; it lets retention reclaim freed pages with
    // `PRAGMA incremental_vacuum` (spec §259, size cap) without a full VACUUM.
    return db_.exec(
        "PRAGMA auto_vacuum=INCREMENTAL;"
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "PRAGMA foreign_keys=ON;"
        "PRAGMA busy_timeout=5000;"
        "PRAGMA temp_store=MEMORY;");
}

bool EventStore::init_schema() {
    if (!db_.exec(kSchemaSql)) return false;
    // Record schema + wire-format versions once.
    SqliteStatement st = db_.prepare(
        "INSERT INTO meta(key,value) VALUES(?1,?2) "
        "ON CONFLICT(key) DO NOTHING;");
    if (!st.valid()) return false;
    auto put = [&](const char* k, const std::string& v) {
        st.reset();
        st.bind_text(1, k);
        st.bind_text(2, v);
        st.step();
    };
    put("schema_version", std::to_string(kStorageSchemaVersion));
    put("event_format_version", std::to_string(kEventFormatVersion));
    return true;
}

bool EventStore::open(const std::string& path) {
    if (!db_.open(path)) return false;
    if (!apply_pragmas()) return false;
    return init_schema();
}

bool EventStore::open_memory() {
    if (!db_.open_memory()) return false;
    if (!apply_pragmas()) return false;
    return init_schema();
}

void EventStore::close() { db_.close(); }

int EventStore::schema_version() {
    SqliteStatement st = db_.prepare("SELECT value FROM meta WHERE key='schema_version';");
    if (st.valid() && st.step() == SqliteStatement::Step::Row) {
        return static_cast<int>(std::strtol(st.column_text(0).c_str(), nullptr, 10));
    }
    return 0;
}

bool EventStore::insert_session(const Session& s) {
    SqliteStatement st = db_.prepare(
        "INSERT INTO sessions(id,started_ms,ended_ms,status,app_version) "
        "VALUES(?1,?2,?3,?4,?5) ON CONFLICT(id) DO UPDATE SET "
        "started_ms=excluded.started_ms,status=excluded.status,"
        "app_version=excluded.app_version;");
    if (!st.valid()) return false;
    st.bind_int64(1, static_cast<std::int64_t>(s.id.value));
    st.bind_int64(2, s.started_at_ms);
    if (s.ended_at_ms) {
        st.bind_int64(3, *s.ended_at_ms);
    } else {
        st.bind_null(3);
    }
    st.bind_int64(4, static_cast<std::int64_t>(s.status));
    st.bind_text(5, s.app_version);
    return st.step() == SqliteStatement::Step::Done;
}

bool EventStore::finalize_session(SessionId id, std::int64_t ended_ms,
                                  SessionStatus status) {
    SqliteStatement st = db_.prepare(
        "UPDATE sessions SET ended_ms=?2,status=?3 WHERE id=?1;");
    if (!st.valid()) return false;
    st.bind_int64(1, static_cast<std::int64_t>(id.value));
    st.bind_int64(2, ended_ms);
    st.bind_int64(3, static_cast<std::int64_t>(status));
    return st.step() == SqliteStatement::Step::Done;
}

bool EventStore::upsert_context(const Context& c) {
    SqliteStatement st = db_.prepare(
        "INSERT INTO contexts(id,session_id,process_name,window_title,"
        "process_id,window_handle,first_seen_ms,last_seen_ms) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8) ON CONFLICT(id) DO UPDATE SET "
        "window_title=excluded.window_title,last_seen_ms=excluded.last_seen_ms;");
    if (!st.valid()) return false;
    st.bind_int64(1, static_cast<std::int64_t>(c.id.value));
    st.bind_int64(2, static_cast<std::int64_t>(c.session.value));
    st.bind_text(3, c.process_name);
    st.bind_text(4, c.window_title);
    st.bind_int64(5, static_cast<std::int64_t>(c.process_id));
    st.bind_int64(6, static_cast<std::int64_t>(c.window_handle));
    st.bind_int64(7, c.first_seen_ms);
    st.bind_int64(8, c.last_seen_ms);
    return st.step() == SqliteStatement::Step::Done;
}

bool EventStore::insert_clipboard_entry(const ClipboardEntry& e) {
    SqliteStatement st = db_.prepare(
        "INSERT INTO clipboard_entries(id,session_id,source_context,timestamp_ms,"
        "text,original_size_bytes,content_hash,truncated) "
        "VALUES(?1,?2,?3,?4,?5,?6,?7,?8) ON CONFLICT(id) DO NOTHING;");
    if (!st.valid()) return false;
    st.bind_int64(1, static_cast<std::int64_t>(e.id.value));
    st.bind_int64(2, static_cast<std::int64_t>(e.session.value));
    st.bind_int64(3, static_cast<std::int64_t>(e.source_context.value));
    st.bind_int64(4, e.timestamp_ms);
    st.bind_text(5, e.text);
    st.bind_int64(6, static_cast<std::int64_t>(e.original_size_bytes));
    st.bind_int64(7, static_cast<std::int64_t>(e.content_hash));
    st.bind_int64(8, e.truncated ? 1 : 0);
    return st.step() == SqliteStatement::Step::Done;
}

bool EventStore::insert_event(const Event& e) {
    SqliteStatement st = db_.prepare(
        "INSERT INTO events(id,session_id,context_id,wall_ms,monotonic_ns,"
        "type,category,flags,text,payload) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10) "
        "ON CONFLICT(id) DO NOTHING;");
    if (!st.valid()) return false;
    st.bind_int64(1, static_cast<std::int64_t>(e.id.value));
    st.bind_int64(2, static_cast<std::int64_t>(e.session.value));
    st.bind_int64(3, static_cast<std::int64_t>(e.context.value));
    st.bind_int64(4, e.time.wall_ms);
    st.bind_int64(5, e.time.monotonic_ns);
    st.bind_int64(6, static_cast<std::int64_t>(e.type));
    st.bind_int64(7, static_cast<std::int64_t>(e.category));
    st.bind_int64(8, static_cast<std::int64_t>(e.flags));
    if (auto text = searchable_text(e)) {
        st.bind_text(9, *text);
    } else {
        st.bind_null(9);
    }
    st.bind_blob(10, serialize_payload(e.payload));
    return st.step() == SqliteStatement::Step::Done;
}

bool EventStore::insert_snapshot(const TextSnapshot& s) {
    SqliteStatement st = db_.prepare(
        "INSERT INTO snapshots(id,session_id,context_id,anchor_event,timestamp_ms,"
        "text,confidence,cursor,cursor_known) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9) "
        "ON CONFLICT(id) DO NOTHING;");
    if (!st.valid()) return false;
    st.bind_int64(1, static_cast<std::int64_t>(s.id.value));
    st.bind_int64(2, static_cast<std::int64_t>(s.session.value));
    st.bind_int64(3, static_cast<std::int64_t>(s.context.value));
    st.bind_int64(4, static_cast<std::int64_t>(s.anchor_event.value));
    st.bind_int64(5, s.timestamp_ms);
    st.bind_text(6, s.text);
    st.bind_int64(7, static_cast<std::int64_t>(s.confidence));
    st.bind_int64(8, static_cast<std::int64_t>(s.cursor));
    st.bind_int64(9, s.cursor_known ? 1 : 0);
    return st.step() == SqliteStatement::Step::Done;
}

bool EventStore::save_settings(const std::map<std::string, std::string>& kv) {
    return db_.transaction([&] {
        SqliteStatement st = db_.prepare(
            "INSERT INTO settings(key,value) VALUES(?1,?2) "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value;");
        if (!st.valid()) return false;
        for (const auto& [k, v] : kv) {
            st.reset();
            st.bind_text(1, k);
            st.bind_text(2, v);
            if (st.step() != SqliteStatement::Step::Done) return false;
        }
        return true;
    });
}

std::map<std::string, std::string> EventStore::load_settings() {
    std::map<std::string, std::string> out;
    SqliteStatement st = db_.prepare("SELECT key,value FROM settings;");
    if (!st.valid()) return out;
    while (st.step() == SqliteStatement::Step::Row) {
        out.emplace(st.column_text(0), st.column_text(1));
    }
    return out;
}

std::uint64_t EventStore::scalar_u64(std::string_view sql) {
    SqliteStatement st = db_.prepare(sql);
    if (st.valid() && st.step() == SqliteStatement::Step::Row) {
        return static_cast<std::uint64_t>(st.column_int64(0));
    }
    return 0;
}

std::int64_t EventStore::count_events() {
    return static_cast<std::int64_t>(scalar_u64("SELECT COUNT(*) FROM events;"));
}

std::int64_t EventStore::count_events_in_session(SessionId session) {
    SqliteStatement st =
        db_.prepare("SELECT COUNT(*) FROM events WHERE session_id=?1;");
    if (!st.valid()) return 0;
    st.bind_int64(1, static_cast<std::int64_t>(session.value));
    if (st.step() == SqliteStatement::Step::Row) return st.column_int64(0);
    return 0;
}

std::uint64_t EventStore::max_event_id() {
    return scalar_u64("SELECT COALESCE(MAX(id),0) FROM events;");
}
std::uint64_t EventStore::max_context_id() {
    return scalar_u64("SELECT COALESCE(MAX(id),0) FROM contexts;");
}
std::uint64_t EventStore::max_clipboard_id() {
    return scalar_u64("SELECT COALESCE(MAX(id),0) FROM clipboard_entries;");
}
std::uint64_t EventStore::max_session_id() {
    return scalar_u64("SELECT COALESCE(MAX(id),0) FROM sessions;");
}
std::uint64_t EventStore::max_snapshot_id() {
    return scalar_u64("SELECT COALESCE(MAX(id),0) FROM snapshots;");
}

std::optional<Event> EventStore::get_event(EventId id) {
    SqliteStatement st = db_.prepare(
        "SELECT id,session_id,context_id,wall_ms,monotonic_ns,type,category,"
        "flags,payload FROM events WHERE id=?1;");
    if (!st.valid()) return std::nullopt;
    st.bind_int64(1, static_cast<std::int64_t>(id.value));
    if (st.step() != SqliteStatement::Step::Row) return std::nullopt;
    Event e;
    e.id = EventId{static_cast<std::uint64_t>(st.column_int64(0))};
    e.session = SessionId{static_cast<std::uint64_t>(st.column_int64(1))};
    e.context = ContextId{static_cast<std::uint64_t>(st.column_int64(2))};
    e.time.wall_ms = st.column_int64(3);
    e.time.monotonic_ns = st.column_int64(4);
    e.type = static_cast<EventType>(st.column_int64(5));
    e.category = static_cast<EventCategory>(st.column_int64(6));
    e.flags = static_cast<EventFlags>(st.column_int64(7));
    if (!deserialize_payload(st.column_blob(8), e.payload)) return std::nullopt;
    return e;
}

std::optional<Context> EventStore::get_context(ContextId id) {
    SqliteStatement st = db_.prepare(
        "SELECT id,session_id,process_name,window_title,process_id,window_handle,"
        "first_seen_ms,last_seen_ms FROM contexts WHERE id=?1;");
    if (!st.valid()) return std::nullopt;
    st.bind_int64(1, static_cast<std::int64_t>(id.value));
    if (st.step() != SqliteStatement::Step::Row) return std::nullopt;
    Context c;
    c.id = ContextId{static_cast<std::uint64_t>(st.column_int64(0))};
    c.session = SessionId{static_cast<std::uint64_t>(st.column_int64(1))};
    c.process_name = st.column_text(2);
    c.window_title = st.column_text(3);
    c.process_id = static_cast<std::uint32_t>(st.column_int64(4));
    c.window_handle = static_cast<std::uint64_t>(st.column_int64(5));
    c.first_seen_ms = st.column_int64(6);
    c.last_seen_ms = st.column_int64(7);
    return c;
}

std::optional<ClipboardEntry> EventStore::get_clipboard_entry(
    ClipboardEntryId id) {
    SqliteStatement st = db_.prepare(
        "SELECT id,session_id,source_context,timestamp_ms,text,"
        "original_size_bytes,content_hash,truncated FROM clipboard_entries "
        "WHERE id=?1;");
    if (!st.valid()) return std::nullopt;
    st.bind_int64(1, static_cast<std::int64_t>(id.value));
    if (st.step() != SqliteStatement::Step::Row) return std::nullopt;
    ClipboardEntry e;
    e.id = ClipboardEntryId{static_cast<std::uint64_t>(st.column_int64(0))};
    e.session = SessionId{static_cast<std::uint64_t>(st.column_int64(1))};
    e.source_context = ContextId{static_cast<std::uint64_t>(st.column_int64(2))};
    e.timestamp_ms = st.column_int64(3);
    e.text = st.column_text(4);
    e.original_size_bytes = static_cast<std::uint64_t>(st.column_int64(5));
    e.content_hash = static_cast<std::uint64_t>(st.column_int64(6));
    e.truncated = st.column_int64(7) != 0;
    return e;
}

std::optional<Session> EventStore::get_session(SessionId id) {
    SqliteStatement st = db_.prepare(
        "SELECT id,started_ms,ended_ms,status,app_version FROM sessions "
        "WHERE id=?1;");
    if (!st.valid()) return std::nullopt;
    st.bind_int64(1, static_cast<std::int64_t>(id.value));
    if (st.step() != SqliteStatement::Step::Row) return std::nullopt;
    Session s;
    s.id = SessionId{static_cast<std::uint64_t>(st.column_int64(0))};
    s.started_at_ms = st.column_int64(1);
    if (!st.column_is_null(2)) s.ended_at_ms = st.column_int64(2);
    s.status = static_cast<SessionStatus>(st.column_int64(3));
    s.app_version = st.column_text(4);
    return s;
}

std::optional<TextSnapshot> EventStore::get_snapshot(SnapshotId id) {
    SqliteStatement st = db_.prepare(
        "SELECT id,session_id,context_id,anchor_event,timestamp_ms,text,"
        "confidence,cursor,cursor_known FROM snapshots WHERE id=?1;");
    if (!st.valid()) return std::nullopt;
    st.bind_int64(1, static_cast<std::int64_t>(id.value));
    if (st.step() != SqliteStatement::Step::Row) return std::nullopt;
    TextSnapshot s;
    s.id = SnapshotId{static_cast<std::uint64_t>(st.column_int64(0))};
    s.session = SessionId{static_cast<std::uint64_t>(st.column_int64(1))};
    s.context = ContextId{static_cast<std::uint64_t>(st.column_int64(2))};
    s.anchor_event = EventId{static_cast<std::uint64_t>(st.column_int64(3))};
    s.timestamp_ms = st.column_int64(4);
    s.text = st.column_text(5);
    s.confidence = static_cast<Confidence>(st.column_int64(6));
    s.cursor = static_cast<std::uint64_t>(st.column_int64(7));
    s.cursor_known = st.column_int64(8) != 0;
    return s;
}

}  // namespace ir
