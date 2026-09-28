#pragma once

// OS-independent mapping from the configured Hotkey to the Win32 RegisterHotKey
// modifier bitmask (spec §268, §601-§606). The Win32 MOD_* values are declared
// here so the mapping is unit-testable without <Windows.h>; global_hotkey.cpp
// (Windows) uses this to register the combo.

#include <cstdint>

#include "core/configuration.hpp"

namespace ir {

// Win32 RegisterHotKey fsModifiers flags (winuser.h values).
inline constexpr std::uint32_t kModAlt = 0x0001;
inline constexpr std::uint32_t kModControl = 0x0002;
inline constexpr std::uint32_t kModShift = 0x0004;
inline constexpr std::uint32_t kModWin = 0x0008;
inline constexpr std::uint32_t kModNoRepeat = 0x4000;

// The RegisterHotKey modifier mask for this hotkey. Always ORs in MOD_NOREPEAT
// so holding the combo fires it once, not repeatedly (spec §604).
std::uint32_t to_win32_modifiers(const Hotkey& hk);

// Registerable only if it has a non-modifier key and at least one modifier
// (a bare key would hijack normal typing).
bool is_registerable(const Hotkey& hk);

}  // namespace ir
