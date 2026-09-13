#pragma once

// OS-neutral view of a single low-level keyboard input, extracted from the
// Windows hook payload (KBDLLHOOKSTRUCT + message). Kept free of <Windows.h> so
// the translation logic that consumes it is unit-testable on any platform.

#include <cstdint>

namespace ir {

struct RawKeyboardInput {
    std::uint32_t virtual_key = 0;  // VK code (0..255)
    std::uint32_t scan_code = 0;
    bool key_up = false;   // false = key down, true = key up
    bool extended = false; // LLKHF_EXTENDED
    bool injected = false; // LLKHF_INJECTED (software-generated)
};

}  // namespace ir
