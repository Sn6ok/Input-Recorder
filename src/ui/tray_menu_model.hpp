#pragma once

// OS-independent model for the system-tray tooltip and context-menu labels
// (spec §607-§615). The labels depend only on the recording status and whether
// the main window is visible, so they are computed here and unit-tested; the
// Win32 tray shell just renders them.

#include <string>

#include "ui/app_view_model.hpp"

namespace ir {

struct TrayMenu {
    std::string tooltip;        // hover text on the tray icon
    std::string toggle_label;   // "Pause recording" / "Resume recording"
    std::string show_hide_label;  // "Show window" / "Hide window"
    std::string settings_label = "Settings…";
    std::string exit_label = "Exit";
};

inline TrayMenu tray_menu(RecordingStatus status, bool window_visible) {
    TrayMenu m;
    const bool recording = is_recording(status);
    m.tooltip = recording ? "Input Recorder — Recording"
                          : "Input Recorder — Paused";
    m.toggle_label = recording ? "Pause recording" : "Resume recording";
    m.show_hide_label = window_visible ? "Hide window" : "Show window";
    return m;
}

}  // namespace ir
