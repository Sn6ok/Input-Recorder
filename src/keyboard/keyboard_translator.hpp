#pragma once

// Turns raw keyboard inputs into normalized KeyDown/KeyUp events (spec §30,
// §62, §63, §81, §82). Maintains modifier state and detects auto-repeat.
//
// This is the testable core of keyboard capture: no <Windows.h>, no threads,
// no I/O. The Win32 hook feeds it RawKeyboardInput and stamps the produced
// events with id/session/context.

#include <array>
#include <cstdint>

#include "capture/raw_input.hpp"
#include "core/event.hpp"

namespace ir {

class KeyboardTranslator {
public:
    // Produces a KeyDown or KeyUp event. The event's id/session/context are left
    // at their defaults (assigned downstream); `ts` fills the timestamp.
    // Modifier flags reflect state *after* this key is applied, so a Ctrl press
    // itself carries ModCtrl.
    Event translate(const RawKeyboardInput& raw, Timestamp ts);

    // Modifier flags (ModCtrl|ModShift|ModAlt|ModWin) for the current state.
    EventFlags modifier_flags() const;

    bool is_down(std::uint16_t vk) const {
        return vk < down_.size() ? down_[vk] : false;
    }

    // Clears all tracked key state (e.g. after a session lock, spec §403).
    void reset() { down_.fill(false); }

private:
    std::array<bool, 256> down_{};
};

}  // namespace ir
