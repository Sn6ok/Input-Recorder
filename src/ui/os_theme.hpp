#pragma once

// Reads the current OS app-theme preference so Theme::System can follow it
// (spec §258, §598). Windows-only; unit tests exercise the OS-free resolution
// in theme.hpp instead.

namespace ir {

// True when Windows is set to a dark app theme (registry AppsUseLightTheme==0).
// Returns false if the value cannot be read.
bool system_prefers_dark();

}  // namespace ir
