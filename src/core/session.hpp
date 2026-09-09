#pragma once

// A process session: one run of Input Recorder (spec §128, §129, §162).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/ids.hpp"

namespace ir {

enum class SessionStatus : std::uint8_t {
    Active = 0,       // currently running
    Completed = 1,    // finalized on normal shutdown
    Interrupted = 2,  // previous run ended abnormally (crash/power loss)
};

constexpr std::string_view to_string(SessionStatus s) {
    switch (s) {
        case SessionStatus::Active: return "Active";
        case SessionStatus::Completed: return "Completed";
        case SessionStatus::Interrupted: return "Interrupted";
    }
    return "Active";
}

struct Session {
    SessionId id;
    std::int64_t started_at_ms = 0;               // wall clock, UTC
    std::optional<std::int64_t> ended_at_ms;      // set on finalize
    SessionStatus status = SessionStatus::Active;
    std::string app_version;                      // producing app version

    friend bool operator==(const Session& a, const Session& b) {
        return a.id == b.id && a.started_at_ms == b.started_at_ms &&
               a.ended_at_ms == b.ended_at_ms && a.status == b.status &&
               a.app_version == b.app_version;
    }
};

}  // namespace ir
