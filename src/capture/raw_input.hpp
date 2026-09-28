#pragma once

// OS-neutral view of a single low-level keyboard input, extracted from the
// Windows hook payload (KBDLLHOOKSTRUCT + message). Kept free of <Windows.h> so
// the translation logic that consumes it is unit-testable on any platform.

#include <cstdint>

#include "core/event_data.hpp"  // MouseButton

namespace ir {

struct RawKeyboardInput {
    std::uint32_t virtual_key = 0;  // VK code (0..255)
    std::uint32_t scan_code = 0;
    bool key_up = false;   // false = key down, true = key up
    bool extended = false; // LLKHF_EXTENDED
    bool injected = false; // LLKHF_INJECTED (software-generated)
};

// OS-neutral view of a single low-level mouse input, extracted from the Windows
// WH_MOUSE_LL payload (MSLLHOOKSTRUCT + message). Kept free of <Windows.h> so
// the MouseProcessor that consumes it is unit-testable on any platform
// (spec §115-§119).
enum class RawMouseAction : std::uint8_t {
    Move = 0,
    ButtonDown = 1,
    ButtonUp = 2,
    Wheel = 3,
};

struct RawMouseInput {
    RawMouseAction action = RawMouseAction::Move;
    MouseButton button = MouseButton::None;  // for ButtonDown / ButtonUp
    std::int32_t x = 0;                       // screen coordinates
    std::int32_t y = 0;
    // Signed wheel amount in WHEEL_DELTA (120) units; vertical and horizontal
    // wheels both arrive as Wheel actions (the axis is not persisted since it is
    // irrelevant to text recovery — see docs/architecture.md Phase 6).
    std::int32_t wheel_delta = 0;
    bool injected = false;  // LLMHF_INJECTED (software-generated)
};

}  // namespace ir
