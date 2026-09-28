#pragma once

// OS-neutral snapshot of the foreground window/process, extracted from the
// Windows APIs (GetForegroundWindow / GetWindowThreadProcessId /
// QueryFullProcessImageName / GetWindowText). Kept free of <Windows.h> so the
// ContextTracker that consumes it is unit-testable on any platform
// (spec §84, §121, §199).

#include <cstdint>
#include <string>

namespace ir {

struct WindowObservation {
    std::string process_name;   // e.g. "claude.exe" (UTF-8, base name only)
    std::string window_title;   // e.g. "main.cpp - VS Code" (UTF-8)
    std::uint32_t process_id = 0;
    std::uint64_t window_handle = 0;  // HWND value, run-local only
    std::int64_t wall_ms = 0;         // observation time
};

}  // namespace ir
