#pragma once

// OS-independent formatting of history rows for the UI list controls
// (spec §576-§585). Turns Events / ClipboardEntries / Sessions into short,
// single-line, display-safe strings. Kept OS-free and unit-tested so the Win32
// list view is a thin renderer.

#include <cstddef>
#include <string>
#include <string_view>

#include "core/clipboard_entry.hpp"
#include "core/event.hpp"
#include "core/session.hpp"

namespace ir {

// Truncates to at most `max_code_points` Unicode code points (never splitting a
// character), appends an ellipsis if it was shortened, and replaces control
// characters (newlines/tabs) with spaces so the result is a single clean line.
std::string truncate_for_display(std::string_view utf8,
                                 std::size_t max_code_points = 80);

// "YYYY-MM-DD HH:MM:SS" (UTC) from a wall-clock millisecond timestamp. The
// window may present this in local time; tests use the deterministic UTC form.
std::string format_timestamp(std::int64_t wall_ms);

// One-line description of an event for the history list, e.g.
// "2026-01-02 03:04:05  typed: hello world".
std::string format_event_row(const Event& e);

// One-line description of a clipboard entry.
std::string format_clipboard_row(const ClipboardEntry& e);

// One-line description of a session.
std::string format_session_row(const Session& s);

}  // namespace ir
