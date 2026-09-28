#pragma once

// OS-independent theme model (spec §258, §596-§600). The app resolves the
// user's Theme preference (System/Light/Dark) against the OS setting into a
// single "dark?" boolean, then looks up the palette. Colours are plain
// 0xRRGGBB integers so the model is unit-testable; the Win32 layer maps them to
// COLORREF.

#include <cstdint>

#include "core/configuration.hpp"

namespace ir {

struct ThemeColors {
    std::uint32_t window_bg;
    std::uint32_t control_bg;
    std::uint32_t text;
    std::uint32_t secondary_text;
    std::uint32_t border;
    std::uint32_t accent_recording;  // indicator while recording
    std::uint32_t accent_paused;     // indicator while paused
};

// Resolves the effective appearance: System follows the OS, Light/Dark are
// explicit.
inline bool effective_dark(Theme t, bool system_prefers_dark) {
    switch (t) {
        case Theme::Light: return false;
        case Theme::Dark: return true;
        case Theme::System: return system_prefers_dark;
    }
    return system_prefers_dark;
}

// The palette for the given appearance.
ThemeColors theme_colors(bool dark);

}  // namespace ir
