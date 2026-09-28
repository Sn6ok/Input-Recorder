#include "history/history_service.hpp"

#include <sstream>
#include <string>
#include <utility>

#include "core/serialization.hpp"
#include "reconstruction/reconstruction_engine.hpp"
#include "utils/unicode.hpp"

namespace ir {
namespace {

// Fixed column order for the events SELECTs consumed by event_from_row().
constexpr const char* kEventCols =
    "id,session_id,context_id,wall_ms,monotonic_ns,type,category,flags,payload";

Event event_from_row(const SqliteStatement& st, int base = 0) {
    Event e;
    e.id = EventId{static_cast<std::uint64_t>(st.column_int64(base + 0))};
    e.session = SessionId{static_cast<std::uint64_t>(st.column_int64(base + 1))};
    e.context = ContextId{static_cast<std::uint64_t>(st.column_int64(base + 2))};
    e.time.wall_ms = st.column_int64(base + 3);
    e.time.monotonic_ns = st.column_int64(base + 4);
    e.type = static_cast<EventType>(st.column_int64(base + 5));
    e.category = static_cast<EventCategory>(st.column_int64(base + 6));
    e.flags = static_cast<EventFlags>(st.column_int64(base + 7));
    deserialize_payload(st.column_blob(base + 8), e.payload);
    return e;
}

constexpr const char* kClipCols =
    "id,session_id,source_context,timestamp_ms,text,original_size_bytes,"
    "content_hash,truncated";

ClipboardEntry clip_from_row(const SqliteStatement& st) {
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

void bind_all(SqliteStatement& st, const std::vector<std::int64_t>& params) {
    for (std::size_t i = 0; i < params.size(); ++i) {
        st.bind_int64(static_cast<int>(i) + 1, params[i]);
    }
}

// Appends WHERE conditions for an EventQuery and their bind values.
void build_event_where(const EventQuery& q, std::string& where,
                       std::vector<std::int64_t>& params) {
    where = " WHERE 1=1";
    if (q.session) {
        where += " AND session_id=?";
        params.push_back(static_cast<std::int64_t>(q.session->value));
    }
    if (q.context) {
        where += " AND context_id=?";
        params.push_back(static_cast<std::int64_t>(q.context->value));
    }
    if (q.type) {
        where += " AND type=?";
        params.push_back(static_cast<std::int64_t>(*q.type));
    }
    if (q.since_ms) {
        where += " AND wall_ms>=?";
        params.push_back(*q.since_ms);
    }
    if (q.until_ms) {
        where += " AND wall_ms<=?";
        params.push_back(*q.until_ms);
    }
}

}  // namespace

std::string make_fts_match(const std::string& user_text) {
    std::istringstream in(user_text);
    std::string token;
    std::string out;
    while (in >> token) {
        std::string quoted = "\"";
        for (char c : token) {
            if (c == '"') quoted += '"';  // escape " as ""
            quoted += c;
        }
        quoted += '"';
        if (!out.empty()) out += ' ';
        out += quoted;
    }
    return out;
}

std::vector<Event> HistoryService::load_events(
    std::string_view sql, const std::vector<std::int64_t>& params) {
    std::vector<Event> out;
    SqliteStatement st = store_.db().prepare(sql);
    if (!st.valid()) return out;
    bind_all(st, params);
    while (st.step() == SqliteStatement::Step::Row) {
        out.push_back(event_from_row(st));
    }
    return out;
}

std::vector<ClipboardEntry> HistoryService::load_clipboard(
    std::string_view sql, const std::vector<std::int64_t>& params) {
    std::vector<ClipboardEntry> out;
    SqliteStatement st = store_.db().prepare(sql);
    if (!st.valid()) return out;
    bind_all(st, params);
    while (st.step() == SqliteStatement::Step::Row) {
        out.push_back(clip_from_row(st));
    }
    return out;
}

std::vector<Event> HistoryService::query_events(const EventQuery& q) {
    std::string where;
    std::vector<std::int64_t> params;
    build_event_where(q, where, params);

    std::string sql = "SELECT ";
    sql += kEventCols;
    sql += " FROM events";
    sql += where;
    sql += q.newest_first ? " ORDER BY wall_ms DESC, id DESC"
                          : " ORDER BY wall_ms ASC, id ASC";
    sql += " LIMIT ? OFFSET ?";
    params.push_back(static_cast<std::int64_t>(q.limit));
    params.push_back(static_cast<std::int64_t>(q.offset));
    return load_events(sql, params);
}

std::int64_t HistoryService::count_events(const EventQuery& q) {
    std::string where;
    std::vector<std::int64_t> params;
    build_event_where(q, where, params);
    std::string sql = "SELECT COUNT(*) FROM events" + where + ";";
    SqliteStatement st = store_.db().prepare(sql);
    if (!st.valid()) return 0;
    bind_all(st, params);
    return st.step() == SqliteStatement::Step::Row ? st.column_int64(0) : 0;
}

std::vector<ClipboardEntry> HistoryService::list_clipboard(
    std::optional<SessionId> session, std::size_t limit, std::size_t offset,
    bool newest_first) {
    std::string sql = "SELECT ";
    sql += kClipCols;
    sql += " FROM clipboard_entries";
    std::vector<std::int64_t> params;
    if (session) {
        sql += " WHERE session_id=?";
        params.push_back(static_cast<std::int64_t>(session->value));
    }
    sql += newest_first ? " ORDER BY timestamp_ms DESC, id DESC"
                        : " ORDER BY timestamp_ms ASC, id ASC";
    sql += " LIMIT ? OFFSET ?";
    params.push_back(static_cast<std::int64_t>(limit));
    params.push_back(static_cast<std::int64_t>(offset));
    return load_clipboard(sql, params);
}

std::vector<Session> HistoryService::list_sessions() {
    std::vector<Session> out;
    SqliteStatement st = store_.db().prepare(
        "SELECT id,started_ms,ended_ms,status,app_version FROM sessions "
        "ORDER BY started_ms DESC, id DESC;");
    if (!st.valid()) return out;
    while (st.step() == SqliteStatement::Step::Row) {
        Session s;
        s.id = SessionId{static_cast<std::uint64_t>(st.column_int64(0))};
        s.started_at_ms = st.column_int64(1);
        if (!st.column_is_null(2)) s.ended_at_ms = st.column_int64(2);
        s.status = static_cast<SessionStatus>(st.column_int64(3));
        s.app_version = st.column_text(4);
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<Event> HistoryService::search_events(const std::string& text,
                                                 std::optional<SessionId> session,
                                                 std::size_t limit,
                                                 std::size_t offset) {
    const std::string match = make_fts_match(text);
    if (match.empty()) return {};

    std::string sql = "SELECT ";
    sql += kEventCols;
    sql +=
        " FROM events_fts JOIN events ON events.id=events_fts.rowid "
        "WHERE events_fts MATCH ?1";
    if (session) sql += " AND events.session_id=?2";
    sql += " ORDER BY events_fts.rank";
    sql += session ? " LIMIT ?3 OFFSET ?4" : " LIMIT ?2 OFFSET ?3";

    std::vector<Event> out;
    SqliteStatement st = store_.db().prepare(sql);
    if (!st.valid()) return out;
    int idx = 1;
    st.bind_text(idx++, match);
    if (session) st.bind_int64(idx++, static_cast<std::int64_t>(session->value));
    st.bind_int64(idx++, static_cast<std::int64_t>(limit));
    st.bind_int64(idx++, static_cast<std::int64_t>(offset));
    while (st.step() == SqliteStatement::Step::Row) {
        out.push_back(event_from_row(st));
    }
    return out;
}

std::int64_t HistoryService::count_search_events(const std::string& text,
                                                 std::optional<SessionId> session) {
    const std::string match = make_fts_match(text);
    if (match.empty()) return 0;
    std::string sql =
        "SELECT COUNT(*) FROM events_fts JOIN events ON events.id=events_fts.rowid "
        "WHERE events_fts MATCH ?1";
    if (session) sql += " AND events.session_id=?2";
    SqliteStatement st = store_.db().prepare(sql);
    if (!st.valid()) return 0;
    st.bind_text(1, match);
    if (session) st.bind_int64(2, static_cast<std::int64_t>(session->value));
    return st.step() == SqliteStatement::Step::Row ? st.column_int64(0) : 0;
}

std::vector<ClipboardEntry> HistoryService::search_clipboard(
    const std::string& text, std::size_t limit, std::size_t offset) {
    const std::string match = make_fts_match(text);
    if (match.empty()) return {};
    // Qualify every column: clip_fts also has a `text` column, so an unqualified
    // list would be ambiguous.
    std::string sql =
        "SELECT clipboard_entries.id,clipboard_entries.session_id,"
        "clipboard_entries.source_context,clipboard_entries.timestamp_ms,"
        "clipboard_entries.text,clipboard_entries.original_size_bytes,"
        "clipboard_entries.content_hash,clipboard_entries.truncated"
        " FROM clip_fts JOIN clipboard_entries "
        "ON clipboard_entries.id=clip_fts.rowid "
        "WHERE clip_fts MATCH ?1 ORDER BY clip_fts.rank LIMIT ?2 OFFSET ?3";
    std::vector<ClipboardEntry> out;
    SqliteStatement st = store_.db().prepare(sql);
    if (!st.valid()) return out;
    st.bind_text(1, match);
    st.bind_int64(2, static_cast<std::int64_t>(limit));
    st.bind_int64(3, static_cast<std::int64_t>(offset));
    while (st.step() == SqliteStatement::Step::Row) {
        out.push_back(clip_from_row(st));
    }
    return out;
}

std::vector<TextSnapshot> HistoryService::snapshots_for_session(SessionId session) {
    std::vector<TextSnapshot> out;
    SqliteStatement st = store_.db().prepare(
        "SELECT id FROM snapshots WHERE session_id=?1 ORDER BY anchor_event ASC;");
    if (!st.valid()) return out;
    st.bind_int64(1, static_cast<std::int64_t>(session.value));
    std::vector<SnapshotId> ids;
    while (st.step() == SqliteStatement::Step::Row) {
        ids.push_back(SnapshotId{static_cast<std::uint64_t>(st.column_int64(0))});
    }
    for (SnapshotId id : ids) {
        if (auto s = store_.get_snapshot(id)) out.push_back(std::move(*s));
    }
    return out;
}

std::optional<TextSnapshot> HistoryService::latest_snapshot_at_or_before(
    SessionId session, EventId anchor) {
    SqliteStatement st = store_.db().prepare(
        "SELECT id FROM snapshots WHERE session_id=?1 AND anchor_event<=?2 "
        "ORDER BY anchor_event DESC LIMIT 1;");
    if (!st.valid()) return std::nullopt;
    st.bind_int64(1, static_cast<std::int64_t>(session.value));
    st.bind_int64(2, static_cast<std::int64_t>(anchor.value));
    if (st.step() != SqliteStatement::Step::Row) return std::nullopt;
    return store_.get_snapshot(
        SnapshotId{static_cast<std::uint64_t>(st.column_int64(0))});
}

Reconstruction HistoryService::replay(std::string_view where_sql,
                                      const std::vector<std::int64_t>& params,
                                      const TextSnapshot* start) {
    ReconstructionEngine engine;
    if (start != nullptr) {
        TextState ts;
        ts.buffer = utf8_to_utf32(start->text);
        ts.cursor = static_cast<std::size_t>(start->cursor);
        ts.cursor_known = start->cursor_known;
        ts.selection_active = false;
        ts.confidence = start->confidence;
        engine.restore(ts);
    }

    std::string sql = "SELECT ";
    sql += kEventCols;
    sql += " FROM events";
    sql += where_sql;
    sql += " ORDER BY monotonic_ns ASC, id ASC;";

    SqliteStatement st = store_.db().prepare(sql);
    if (st.valid()) {
        bind_all(st, params);
        while (st.step() == SqliteStatement::Step::Row) {
            engine.process(event_from_row(st));
        }
    }
    return Reconstruction{engine.text(), engine.confidence(), engine.cursor(),
                          engine.cursor_known()};
}

Reconstruction HistoryService::reconstruct_full(SessionId session, EventId up_to,
                                                std::optional<ContextId> context) {
    std::string where = " WHERE session_id=? AND id<=?";
    std::vector<std::int64_t> params{static_cast<std::int64_t>(session.value),
                                     static_cast<std::int64_t>(up_to.value)};
    if (context) {
        where += " AND context_id=?";
        params.push_back(static_cast<std::int64_t>(context->value));
    }
    return replay(where, params, nullptr);
}

Reconstruction HistoryService::reconstruct_at(SessionId session, EventId up_to,
                                              std::optional<ContextId> context) {
    std::optional<TextSnapshot> snap = latest_snapshot_at_or_before(session, up_to);
    if (context && snap && snap->context != *context) snap.reset();
    if (!snap) return reconstruct_full(session, up_to, context);

    std::string where = " WHERE session_id=? AND id>? AND id<=?";
    std::vector<std::int64_t> params{
        static_cast<std::int64_t>(session.value),
        static_cast<std::int64_t>(snap->anchor_event.value),
        static_cast<std::int64_t>(up_to.value)};
    if (context) {
        where += " AND context_id=?";
        params.push_back(static_cast<std::int64_t>(context->value));
    }
    return replay(where, params, &*snap);
}

// ---- retention ----------------------------------------------------------

namespace {
std::int64_t run_delete(SqliteDatabase& db, std::string_view sql,
                        const std::vector<std::int64_t>& params) {
    SqliteStatement st = db.prepare(sql);
    if (!st.valid()) return 0;
    for (std::size_t i = 0; i < params.size(); ++i)
        st.bind_int64(static_cast<int>(i) + 1, params[i]);
    if (st.step() != SqliteStatement::Step::Done) return 0;
    return db.changes();
}
}  // namespace

RetentionResult HistoryService::purge_before(std::int64_t cutoff_ms) {
    RetentionResult r;
    SqliteDatabase& db = store_.db();
    db.transaction([&] {
        r.events = run_delete(db, "DELETE FROM events WHERE wall_ms<?1;", {cutoff_ms});
        r.clipboard = run_delete(
            db, "DELETE FROM clipboard_entries WHERE timestamp_ms<?1;", {cutoff_ms});
        r.snapshots = run_delete(
            db, "DELETE FROM snapshots WHERE timestamp_ms<?1;", {cutoff_ms});
        r.contexts = run_delete(
            db,
            "DELETE FROM contexts WHERE last_seen_ms<?1 AND id NOT IN "
            "(SELECT DISTINCT context_id FROM events);",
            {cutoff_ms});
        r.sessions = run_delete(
            db,
            "DELETE FROM sessions WHERE ended_ms IS NOT NULL AND ended_ms<?1 "
            "AND id NOT IN (SELECT DISTINCT session_id FROM events);",
            {cutoff_ms});
        return true;
    });
    db.exec("PRAGMA incremental_vacuum;");
    return r;
}

RetentionResult HistoryService::apply_retention(std::int64_t now_ms,
                                                std::int32_t retention_days) {
    if (retention_days <= 0) return RetentionResult{};  // keep indefinitely
    const std::int64_t cutoff =
        now_ms - static_cast<std::int64_t>(retention_days) * 86'400'000;
    return purge_before(cutoff);
}

bool HistoryService::clear_all() {
    SqliteDatabase& db = store_.db();
    const bool ok = db.transaction([&] {
        return db.exec(
            "DELETE FROM events;"
            "DELETE FROM clipboard_entries;"
            "DELETE FROM snapshots;"
            "DELETE FROM contexts;"
            "DELETE FROM sessions;");
    });
    db.exec("PRAGMA incremental_vacuum;");
    return ok;
}

std::int64_t HistoryService::database_size_bytes() {
    SqliteDatabase& db = store_.db();
    std::int64_t pages = 0, page_size = 0;
    if (SqliteStatement st = db.prepare("PRAGMA page_count;");
        st.valid() && st.step() == SqliteStatement::Step::Row) {
        pages = st.column_int64(0);
    }
    if (SqliteStatement st = db.prepare("PRAGMA page_size;");
        st.valid() && st.step() == SqliteStatement::Step::Row) {
        page_size = st.column_int64(0);
    }
    return pages * page_size;
}

RetentionResult HistoryService::enforce_size_cap(std::uint64_t max_bytes) {
    RetentionResult total;
    SqliteDatabase& db = store_.db();
    const auto cap = static_cast<std::int64_t>(max_bytes);

    for (int pass = 0; pass < 128; ++pass) {
        if (database_size_bytes() <= cap) break;
        // Delete the oldest batch of events; FTS is kept in sync by triggers.
        const std::int64_t deleted = run_delete(
            db,
            "DELETE FROM events WHERE id IN "
            "(SELECT id FROM events ORDER BY monotonic_ns ASC, id ASC LIMIT 500);",
            {});
        if (deleted == 0) break;  // nothing left to trim
        total.events += deleted;
        db.exec("PRAGMA incremental_vacuum;");
    }

    // Drop clipboard entries, snapshots and contexts orphaned by the trimming.
    total.contexts += run_delete(
        db,
        "DELETE FROM contexts WHERE id NOT IN (SELECT DISTINCT context_id FROM events);",
        {});
    db.exec("PRAGMA incremental_vacuum;");
    return total;
}

}  // namespace ir
