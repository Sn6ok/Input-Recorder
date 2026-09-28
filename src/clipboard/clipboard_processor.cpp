#include "clipboard/clipboard_processor.hpp"

#include "utils/unicode.hpp"

namespace ir {

ClipboardProcessor::ClipboardProcessor(EventIdAllocator& ids, SessionId session,
                                       std::uint64_t max_bytes)
    : ids_(ids), session_(session), max_bytes_(max_bytes == 0 ? 1 : max_bytes) {}

void ClipboardProcessor::note_self_copy(const std::string& utf8) {
    self_copy_pending_ = true;
    self_copy_hash_ = fnv1a_64(utf8);
}

ClipboardResult ClipboardProcessor::on_clipboard_text(const std::string& utf8,
                                                      std::int64_t wall_ms,
                                                      std::int64_t monotonic_ns,
                                                      ContextId context) {
    const std::uint64_t hash = fnv1a_64(utf8);

    // Suppress the app's own copy (e.g. Copy All) so it is not a user event.
    if (self_copy_pending_ && hash == self_copy_hash_) {
        self_copy_pending_ = false;
        last_hash_ = hash;
        has_last_ = true;
        return ClipboardResult{std::nullopt, std::nullopt, /*suppressed=*/true};
    }

    // Suppress an unchanged clipboard (identical consecutive content, §106).
    if (has_last_ && hash == last_hash_) {
        return ClipboardResult{std::nullopt, std::nullopt, /*suppressed=*/true};
    }

    last_hash_ = hash;
    has_last_ = true;

    // Enforce the size cap (§111): store a truncated copy but remember the full
    // size and flag truncation.
    const bool truncated = utf8.size() > max_bytes_;
    std::string stored = truncated ? utf8_truncate(utf8, max_bytes_) : utf8;

    ClipboardEntry entry;
    entry.id = ClipboardEntryId{next_entry_id_++};
    entry.session = session_;
    entry.source_context = context;
    entry.timestamp_ms = wall_ms;
    entry.text = std::move(stored);
    entry.original_size_bytes = utf8.size();
    entry.content_hash = hash;
    entry.truncated = truncated;

    Event ev;
    ev.id = ids_.next();
    ev.session = session_;
    ev.context = context;
    ev.time = Timestamp{wall_ms, monotonic_ns};
    ev.type = EventType::ClipboardChanged;
    ev.category = EventCategory::ClipboardEvent;
    ev.flags = truncated ? EventFlags::Truncated : EventFlags::None;
    ev.payload = ClipboardRef{entry.id};

    ClipboardResult result;
    result.entry = std::move(entry);
    result.event = std::move(ev);
    result.suppressed = false;
    return result;
}

Event ClipboardProcessor::make_paste_event(const std::string& clipboard_utf8,
                                           std::int64_t wall_ms,
                                           std::int64_t monotonic_ns,
                                           ContextId context) {
    const bool truncated = clipboard_utf8.size() > max_bytes_;
    std::string text =
        truncated ? utf8_truncate(clipboard_utf8, max_bytes_) : clipboard_utf8;

    Event ev;
    ev.id = ids_.next();
    ev.session = session_;
    ev.context = context;
    ev.time = Timestamp{wall_ms, monotonic_ns};
    ev.type = EventType::Paste;
    ev.category = EventCategory::ClipboardEvent;
    ev.flags = truncated ? EventFlags::Truncated : EventFlags::None;
    ev.payload = TextInputData{std::move(text)};  // resolved paste text
    return ev;
}

}  // namespace ir
