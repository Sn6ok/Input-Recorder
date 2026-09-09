#pragma once

// Application configuration: every user-facing setting in one place with sane
// defaults (spec §45, §253-§258, §268, §654). The rest of the app reads config
// only through the SettingsService (added later); this struct is the model.

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace ir {

enum class Theme : std::uint8_t {
    System = 0,
    Light = 1,
    Dark = 2,
};

std::string_view to_string(Theme t);
Theme theme_from_string(std::string_view s, Theme fallback = Theme::System);

// A global hotkey combination (spec §268). Default toggle is Ctrl+Shift+R
// (spec §88).
struct Hotkey {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    bool win = false;
    std::uint16_t virtual_key = 0;  // e.g. 0x52 == 'R'

    friend bool operator==(const Hotkey& a, const Hotkey& b) {
        return a.ctrl == b.ctrl && a.shift == b.shift && a.alt == b.alt &&
               a.win == b.win && a.virtual_key == b.virtual_key;
    }
    // "Ctrl+Shift+R" style label.
    std::string to_string() const;
    // Parses a "Ctrl+Shift+R" style label. Returns false on failure (out is
    // left unchanged).
    static bool parse(std::string_view text, Hotkey& out);
};

struct Configuration {
    // General (spec §253)
    bool start_with_windows = false;
    bool start_minimized = false;
    bool minimize_to_tray = true;
    bool close_to_tray = true;
    bool show_notifications = true;

    // Recording (spec §254). Defaults: keyboard/mouse clicks/wheel/clipboard/
    // active-window ON, mouse movement OFF.
    bool enable_recording = true;
    bool record_keyboard = true;
    bool record_mouse_clicks = true;
    bool record_mouse_wheel = true;
    bool record_mouse_movement = false;
    bool track_active_window = true;

    // Mouse (spec §255): throttle interval when movement recording is enabled.
    std::int32_t mouse_movement_sampling_ms = 100;

    // Clipboard (spec §256)
    bool monitor_clipboard = true;
    bool record_text_clipboard = true;
    std::uint64_t max_clipboard_item_bytes = 1u << 20;  // 1 MiB

    // History (spec §257). retention_days == 0 means keep indefinitely.
    std::int32_t retention_days = 90;
    std::uint64_t max_storage_bytes = 2ull * 1024 * 1024 * 1024;  // 2 GiB
    bool automatic_cleanup = true;
    bool create_snapshots = true;
    std::int32_t snapshot_event_interval = 200;      // events between snapshots
    std::int32_t snapshot_time_interval_ms = 30000;  // or every 30s

    // Appearance (spec §258): default System.
    Theme theme = Theme::System;

    // Window behavior (spec §654): Always on Top off by default.
    bool always_on_top = false;

    // Global toggle hotkey (spec §88, §268): Ctrl+Shift+R.
    Hotkey toggle_hotkey{true, true, false, false, 0x52};

    friend bool operator==(const Configuration& a, const Configuration& b);
    friend bool operator!=(const Configuration& a, const Configuration& b) {
        return !(a == b);
    }
};

// Clamps out-of-range values into valid bounds in place (spec §294).
void validate(Configuration& config);

// Serialize to / from a flat key=value map (the settings table representation,
// spec §197). from_key_values starts from defaults and applies known keys;
// unknown keys are ignored and missing keys keep their default.
std::map<std::string, std::string> to_key_values(const Configuration& config);
Configuration configuration_from_key_values(
    const std::map<std::string, std::string>& kv);

}  // namespace ir
