#pragma once

// Application/window context an event occurred in (spec §84, §121, §199).
// PID and HWND are only valid within the current run and must not be treated as
// permanent identifiers (spec §126, §127); process_name + window_title are the
// user-meaningful fields.

#include <cstdint>
#include <string>

#include "core/ids.hpp"

namespace ir {

struct Context {
    ContextId id;
    SessionId session;
    std::string process_name;   // e.g. "claude.exe" (UTF-8)
    std::string window_title;   // e.g. "main.cpp - VS Code" (UTF-8)
    std::uint32_t process_id = 0;
    std::uint64_t window_handle = 0;  // HWND value, run-local only
    std::int64_t first_seen_ms = 0;
    std::int64_t last_seen_ms = 0;

    friend bool operator==(const Context& a, const Context& b) {
        return a.id == b.id && a.session == b.session &&
               a.process_name == b.process_name && a.window_title == b.window_title &&
               a.process_id == b.process_id && a.window_handle == b.window_handle &&
               a.first_seen_ms == b.first_seen_ms && a.last_seen_ms == b.last_seen_ms;
    }

    // Two observations refer to the same logical context when the process and
    // window identity match (used for dedup, spec §200). Titles can change
    // within one context, so a title change alone is handled separately.
    bool same_identity_as(const Context& other) const {
        return process_id == other.process_id &&
               window_handle == other.window_handle &&
               process_name == other.process_name;
    }
};

}  // namespace ir
