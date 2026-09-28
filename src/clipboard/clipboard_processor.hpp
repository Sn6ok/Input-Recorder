#pragma once

// OS-independent clipboard logic (spec §101-§113). The Win32 monitor feeds it
// observed clipboard text; the processor applies:
//   * consecutive-duplicate suppression (spec §106)
//   * a configurable size cap with truncation on a code-point boundary (§111)
//   * self-copy suppression so the app's own "Copy All" is not recorded as a
//     user clipboard event (spec §83, §497)
// and produces a ClipboardEntry (stored separately) plus a ClipboardChanged
// event referencing it (spec §104, §109).
//
// Testable without <Windows.h>: input is already UTF-8.

#include <cstdint>
#include <optional>
#include <string>

#include "core/clipboard_entry.hpp"
#include "core/event.hpp"
#include "core/event_id_allocator.hpp"
#include "core/ids.hpp"

namespace ir {

struct ClipboardResult {
    std::optional<ClipboardEntry> entry;  // new entry, if this was a real change
    std::optional<Event> event;           // ClipboardChanged event referencing it
    bool suppressed = false;              // true if ignored (duplicate/self-copy)
};

class ClipboardProcessor {
public:
    ClipboardProcessor(EventIdAllocator& ids, SessionId session,
                       std::uint64_t max_bytes);

    // Handle observed clipboard text. Returns a new entry + event on a genuine
    // change, or a suppressed result for a duplicate/self-generated change.
    ClipboardResult on_clipboard_text(const std::string& utf8, std::int64_t wall_ms,
                                      std::int64_t monotonic_ns, ContextId context);

    // Record that the application is about to place `utf8` on the clipboard
    // itself (e.g. Copy All). The next matching change is suppressed (§497).
    void note_self_copy(const std::string& utf8);

    // Build a Paste event carrying resolved clipboard text so reconstruction can
    // insert it (spec §108, §496). Does not touch dedup state.
    Event make_paste_event(const std::string& clipboard_utf8, std::int64_t wall_ms,
                           std::int64_t monotonic_ns, ContextId context);

    void set_max_bytes(std::uint64_t max_bytes) { max_bytes_ = max_bytes; }
    std::uint64_t max_bytes() const { return max_bytes_; }

private:
    EventIdAllocator& ids_;
    SessionId session_;
    std::uint64_t max_bytes_;
    std::uint64_t next_entry_id_ = 1;

    bool has_last_ = false;
    std::uint64_t last_hash_ = 0;

    bool self_copy_pending_ = false;
    std::uint64_t self_copy_hash_ = 0;
};

}  // namespace ir
