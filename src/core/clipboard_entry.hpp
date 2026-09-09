#pragma once

// A captured clipboard text entry, stored once and referenced by events
// (spec §104, §106, §109, §111). Only Unicode text is captured in V1.

#include <cstdint>
#include <string>
#include <string_view>

#include "core/ids.hpp"

namespace ir {

struct ClipboardEntry {
    ClipboardEntryId id;
    SessionId session;
    ContextId source_context;   // where the change was observed (0 if unknown)
    std::int64_t timestamp_ms = 0;
    std::string text;           // UTF-8; possibly truncated (see `truncated`)
    std::uint64_t original_size_bytes = 0;  // full size before any truncation
    std::uint64_t content_hash = 0;         // for dedup of identical content
    bool truncated = false;

    friend bool operator==(const ClipboardEntry& a, const ClipboardEntry& b) {
        return a.id == b.id && a.session == b.session &&
               a.source_context == b.source_context &&
               a.timestamp_ms == b.timestamp_ms && a.text == b.text &&
               a.original_size_bytes == b.original_size_bytes &&
               a.content_hash == b.content_hash && a.truncated == b.truncated;
    }
};

// Stable 64-bit FNV-1a hash of UTF-8 content, used to detect duplicate
// clipboard content cheaply without comparing full strings (spec §106, §185).
inline std::uint64_t fnv1a_64(std::string_view data) {
    std::uint64_t hash = 1469598103934665603ULL;  // FNV offset basis
    for (char c : data) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= 1099511628211ULL;  // FNV prime
    }
    return hash;
}

}  // namespace ir
