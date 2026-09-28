#pragma once

// Persistent event/context/clipboard/session/snapshot store on top of SQLite
// (spec §162-§185). Owns the schema, the typed columns + serialized payload
// blob per event (hybrid layout, spec §166/§167), WAL journaling and the
// pragmas. All writes are explicit-id inserts so the in-memory 64-bit ids are
// the on-disk primary keys; the max_*_id() queries let the app seed its
// allocators at startup so ids stay globally unique across runs (spec §445).
//
// OS-free and unit-tested (temp files / :memory:). The async StorageWorker
// drives batched writes through this store; the UI reads through it too.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/clipboard_entry.hpp"
#include "core/context.hpp"
#include "core/event.hpp"
#include "core/session.hpp"
#include "core/text_snapshot.hpp"
#include "storage/sqlite_database.hpp"

namespace ir {

// Current storage schema version (distinct from the event wire format version).
inline constexpr int kStorageSchemaVersion = 1;

class EventStore {
public:
    // Opens (creating if needed) the database at `path`, applies pragmas
    // (WAL, synchronous=NORMAL, foreign_keys, busy_timeout) and ensures the
    // schema. Returns false on failure (see last_error()).
    bool open(const std::string& path);
    bool open_memory();  // tests
    void close();
    bool is_open() const { return db_.is_open(); }

    const std::string& last_error() const { return db_.last_error(); }
    SqliteDatabase& db() { return db_; }  // for the worker's batched transactions

    int schema_version();

    // ---- writes (safe individually; batch them in a db().transaction()) ----
    bool insert_session(const Session& s);
    bool finalize_session(SessionId id, std::int64_t ended_ms,
                          SessionStatus status);
    bool upsert_context(const Context& c);
    bool insert_clipboard_entry(const ClipboardEntry& e);
    bool insert_event(const Event& e);
    bool insert_snapshot(const TextSnapshot& s);

    // ---- settings (flat key/value; spec §197) ----
    bool save_settings(const std::map<std::string, std::string>& kv);
    std::map<std::string, std::string> load_settings();

    // ---- counts / seeding ----
    std::int64_t count_events();
    std::int64_t count_events_in_session(SessionId session);
    std::uint64_t max_event_id();
    std::uint64_t max_context_id();
    std::uint64_t max_clipboard_id();
    std::uint64_t max_session_id();
    std::uint64_t max_snapshot_id();

    // ---- basic reads (Phase 9 adds pagination/search) ----
    std::optional<Event> get_event(EventId id);
    std::optional<Context> get_context(ContextId id);
    std::optional<ClipboardEntry> get_clipboard_entry(ClipboardEntryId id);
    std::optional<Session> get_session(SessionId id);
    std::optional<TextSnapshot> get_snapshot(SnapshotId id);

private:
    bool init_schema();
    bool apply_pragmas();
    std::uint64_t scalar_u64(std::string_view sql);

    SqliteDatabase db_;
};

// Extracts the user-meaningful text an event should expose to full-text search
// (TextInput / Paste resolved text / shortcut label). Returns nullopt for
// events with no searchable user text (spec §187). Never returns Diagnostic
// content (spec §322).
std::optional<std::string> searchable_text(const Event& e);

}  // namespace ir
