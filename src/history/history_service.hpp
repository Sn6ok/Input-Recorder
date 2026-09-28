#pragma once

// Read/maintenance layer over EventStore (spec §186-§196, §257, §259). Provides:
//   * paginated, filtered event and clipboard listings,
//   * full-text search over recorded text and clipboard content (FTS5),
//   * point-in-time reconstruction accelerated by stored snapshots,
//   * retention: age-based purge, "delete all", and a storage size cap.
// OS-free and unit-tested against a real (temp/in-memory) database.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/clipboard_entry.hpp"
#include "core/confidence.hpp"
#include "core/context.hpp"
#include "core/event.hpp"
#include "core/session.hpp"
#include "core/text_snapshot.hpp"
#include "storage/event_store.hpp"

namespace ir {

// Filter + paging for an event listing. Unset optionals mean "no filter".
struct EventQuery {
    std::optional<SessionId> session;
    std::optional<ContextId> context;
    std::optional<EventType> type;
    std::optional<std::int64_t> since_ms;  // inclusive lower bound on wall_ms
    std::optional<std::int64_t> until_ms;  // inclusive upper bound on wall_ms
    std::size_t limit = 100;
    std::size_t offset = 0;
    bool newest_first = true;
};

// Result of a point-in-time reconstruction.
struct Reconstruction {
    std::string text;
    Confidence confidence = Confidence::High;
    std::size_t cursor = 0;
    bool cursor_known = true;
};

// Counts of rows removed by a retention operation.
struct RetentionResult {
    std::int64_t events = 0;
    std::int64_t clipboard = 0;
    std::int64_t snapshots = 0;
    std::int64_t contexts = 0;
    std::int64_t sessions = 0;

    std::int64_t total() const {
        return events + clipboard + snapshots + contexts + sessions;
    }
};

class HistoryService {
public:
    explicit HistoryService(EventStore& store) : store_(store) {}

    // ---- paginated listings ----
    std::vector<Event> query_events(const EventQuery& q);
    std::int64_t count_events(const EventQuery& q);  // ignores limit/offset
    std::vector<ClipboardEntry> list_clipboard(std::optional<SessionId> session,
                                               std::size_t limit,
                                               std::size_t offset,
                                               bool newest_first = true);
    std::vector<Session> list_sessions();

    // ---- full-text search ----
    std::vector<Event> search_events(const std::string& text,
                                     std::optional<SessionId> session,
                                     std::size_t limit, std::size_t offset);
    std::int64_t count_search_events(const std::string& text,
                                     std::optional<SessionId> session);
    std::vector<ClipboardEntry> search_clipboard(const std::string& text,
                                                 std::size_t limit,
                                                 std::size_t offset);

    // ---- snapshots ----
    std::vector<TextSnapshot> snapshots_for_session(SessionId session);
    std::optional<TextSnapshot> latest_snapshot_at_or_before(SessionId session,
                                                             EventId anchor);

    // ---- point-in-time reconstruction ----
    // Full replay of a session's (optionally single-context) events up to and
    // including `up_to`. Correct but O(events).
    Reconstruction reconstruct_full(SessionId session, EventId up_to,
                                    std::optional<ContextId> context = {});
    // Same result, accelerated by the latest snapshot at/before `up_to`.
    Reconstruction reconstruct_at(SessionId session, EventId up_to,
                                  std::optional<ContextId> context = {});

    // ---- retention ----
    RetentionResult purge_before(std::int64_t cutoff_ms);
    // retention_days <= 0 keeps history indefinitely (no-op).
    RetentionResult apply_retention(std::int64_t now_ms,
                                    std::int32_t retention_days);
    bool clear_all();
    std::int64_t database_size_bytes();
    // Deletes oldest events (and orphaned rows) until the database is at or
    // below max_bytes, reclaiming freed pages. Bounded number of passes.
    RetentionResult enforce_size_cap(std::uint64_t max_bytes);

private:
    std::vector<Event> load_events(std::string_view sql,
                                   const std::vector<std::int64_t>& params);
    std::vector<ClipboardEntry> load_clipboard(std::string_view sql,
                                               const std::vector<std::int64_t>& params);
    Reconstruction replay(std::string_view where_sql,
                          const std::vector<std::int64_t>& params,
                          const TextSnapshot* start);

    EventStore& store_;
};

// Builds a safe FTS5 MATCH expression from arbitrary user text: each
// whitespace-separated token becomes a quoted phrase (AND-ed), so punctuation
// and FTS operators in user input can never cause a query error. Returns empty
// if the input has no usable tokens.
std::string make_fts_match(const std::string& user_text);

}  // namespace ir
