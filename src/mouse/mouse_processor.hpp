#pragma once

// Turns raw mouse inputs into normalized MouseButton / MouseWheel / MouseMove
// events (spec §115-§120). Applies the recording toggles and throttles mouse
// movement to the configured sampling interval so a moving mouse cannot flood
// the pipeline (spec §116, §117, §255).
//
// This is the testable core of mouse capture: no <Windows.h>, no threads, no
// I/O. The Win32 hook feeds it RawMouseInput and stamps the produced events with
// id/session/context. State (last-move time) is confined to the capture thread.

#include <cstdint>
#include <optional>

#include "capture/raw_input.hpp"
#include "core/event.hpp"

namespace ir {

// The recording toggles the processor needs, mirrored from Configuration
// (spec §254, §255). The hook refreshes these when settings change.
struct MouseSettings {
    bool record_buttons = true;
    bool record_wheel = true;
    bool record_movement = false;      // off by default (spec §116)
    std::int32_t movement_sampling_ms = 100;  // throttle when movement is on
};

class MouseProcessor {
public:
    MouseProcessor() = default;
    explicit MouseProcessor(const MouseSettings& settings) : settings_(settings) {}

    // Produces an event, or std::nullopt when the input is filtered out
    // (category disabled, or a movement sample throttled). The event's
    // id/session/context are left at defaults (assigned downstream); `ts` fills
    // the timestamp.
    std::optional<Event> process(const RawMouseInput& raw, Timestamp ts);

    void set_settings(const MouseSettings& s) { settings_ = s; }
    const MouseSettings& settings() const { return settings_; }

    // Clears throttling state (e.g. after a pause/resume, spec §403).
    void reset() { has_last_move_ = false; }

private:
    bool movement_due(std::int64_t monotonic_ns) const;

    MouseSettings settings_{};
    bool has_last_move_ = false;
    std::int64_t last_move_monotonic_ns_ = 0;
};

}  // namespace ir
