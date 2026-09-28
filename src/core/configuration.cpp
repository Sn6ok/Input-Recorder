#include "core/configuration.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <optional>

namespace ir {
namespace {

std::optional<bool> parse_bool(std::string_view s) {
    if (s == "true" || s == "1" || s == "on" || s == "yes") return true;
    if (s == "false" || s == "0" || s == "off" || s == "no") return false;
    return std::nullopt;
}

std::optional<std::int64_t> parse_i64(std::string_view s) {
    std::int64_t v = 0;
    auto* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(s.data(), end, v);
    if (ec == std::errc{} && ptr == end) return v;
    return std::nullopt;
}

std::optional<std::uint64_t> parse_u64(std::string_view s) {
    std::uint64_t v = 0;
    auto* end = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(s.data(), end, v);
    if (ec == std::errc{} && ptr == end) return v;
    return std::nullopt;
}

bool map_bool(const std::map<std::string, std::string>& kv, const char* key, bool def) {
    auto it = kv.find(key);
    if (it == kv.end()) return def;
    return parse_bool(it->second).value_or(def);
}

std::int32_t map_i32(const std::map<std::string, std::string>& kv, const char* key,
                     std::int32_t def) {
    auto it = kv.find(key);
    if (it == kv.end()) return def;
    if (auto v = parse_i64(it->second)) {
        return static_cast<std::int32_t>(
            std::clamp<std::int64_t>(*v, INT32_MIN, INT32_MAX));
    }
    return def;
}

std::uint64_t map_u64(const std::map<std::string, std::string>& kv, const char* key,
                      std::uint64_t def) {
    auto it = kv.find(key);
    if (it == kv.end()) return def;
    return parse_u64(it->second).value_or(def);
}

template <typename T>
std::string num_to_string(T v) {
    return std::to_string(v);
}

}  // namespace

std::string_view to_string(Theme t) {
    switch (t) {
        case Theme::System: return "System";
        case Theme::Light: return "Light";
        case Theme::Dark: return "Dark";
    }
    return "System";
}

Theme theme_from_string(std::string_view s, Theme fallback) {
    if (s == "System") return Theme::System;
    if (s == "Light") return Theme::Light;
    if (s == "Dark") return Theme::Dark;
    return fallback;
}

// ---- Hotkey --------------------------------------------------------------

namespace {

// Function keys VK_F1..VK_F24 are 0x70..0x87.
std::optional<std::uint16_t> key_name_to_vk(std::string_view name) {
    if (name.size() == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            return static_cast<std::uint16_t>(c);
        }
    }
    if ((name.size() == 2 || name.size() == 3) && (name[0] == 'F' || name[0] == 'f')) {
        if (auto n = parse_i64(name.substr(1)); n && *n >= 1 && *n <= 24) {
            return static_cast<std::uint16_t>(0x70 + (*n - 1));
        }
    }
    // Explicit hex form "VK_1B".
    if (name.size() > 3 && (name.substr(0, 3) == "VK_" || name.substr(0, 3) == "vk_")) {
        std::uint16_t v = 0;
        auto sub = name.substr(3);
        auto* end = sub.data() + sub.size();
        auto [ptr, ec] = std::from_chars(sub.data(), end, v, 16);
        if (ec == std::errc{} && ptr == end) return v;
    }
    return std::nullopt;
}

std::string vk_to_key_name(std::uint16_t vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return std::string(1, static_cast<char>(vk));
    }
    if (vk >= 0x70 && vk <= 0x87) {
        return "F" + std::to_string(vk - 0x70 + 1);
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "VK_%02X", vk);
    return std::string(buf);
}

}  // namespace

std::string Hotkey::to_string() const {
    std::string out;
    if (ctrl) out += "Ctrl+";
    if (shift) out += "Shift+";
    if (alt) out += "Alt+";
    if (win) out += "Win+";
    out += vk_to_key_name(virtual_key);
    return out;
}

bool Hotkey::parse(std::string_view text, Hotkey& out) {
    Hotkey hk;
    std::size_t start = 0;
    bool got_key = false;
    while (start <= text.size()) {
        std::size_t plus = text.find('+', start);
        std::string_view token = (plus == std::string_view::npos)
                                     ? text.substr(start)
                                     : text.substr(start, plus - start);
        // trim spaces
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
            token.remove_prefix(1);
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
            token.remove_suffix(1);

        if (token == "Ctrl" || token == "Control") {
            hk.ctrl = true;
        } else if (token == "Shift") {
            hk.shift = true;
        } else if (token == "Alt") {
            hk.alt = true;
        } else if (token == "Win" || token == "Meta" || token == "Super") {
            hk.win = true;
        } else if (!token.empty()) {
            if (auto vk = key_name_to_vk(token)) {
                hk.virtual_key = *vk;
                got_key = true;
            } else {
                return false;
            }
        }

        if (plus == std::string_view::npos) break;
        start = plus + 1;
    }
    if (!got_key) return false;
    out = hk;
    return true;
}

// ---- Configuration -------------------------------------------------------

bool operator==(const Configuration& a, const Configuration& b) {
    return a.start_with_windows == b.start_with_windows &&
           a.start_minimized == b.start_minimized &&
           a.minimize_to_tray == b.minimize_to_tray &&
           a.close_to_tray == b.close_to_tray &&
           a.show_notifications == b.show_notifications &&
           a.enable_recording == b.enable_recording &&
           a.record_keyboard == b.record_keyboard &&
           a.record_mouse_clicks == b.record_mouse_clicks &&
           a.record_mouse_wheel == b.record_mouse_wheel &&
           a.record_mouse_movement == b.record_mouse_movement &&
           a.track_active_window == b.track_active_window &&
           a.mouse_movement_sampling_ms == b.mouse_movement_sampling_ms &&
           a.monitor_clipboard == b.monitor_clipboard &&
           a.record_text_clipboard == b.record_text_clipboard &&
           a.max_clipboard_item_bytes == b.max_clipboard_item_bytes &&
           a.retention_days == b.retention_days &&
           a.max_storage_bytes == b.max_storage_bytes &&
           a.automatic_cleanup == b.automatic_cleanup &&
           a.create_snapshots == b.create_snapshots &&
           a.snapshot_event_interval == b.snapshot_event_interval &&
           a.snapshot_time_interval_ms == b.snapshot_time_interval_ms &&
           a.theme == b.theme && a.always_on_top == b.always_on_top &&
           a.toggle_hotkey == b.toggle_hotkey;
}

void validate(Configuration& c) {
    c.mouse_movement_sampling_ms = std::clamp(c.mouse_movement_sampling_ms, 10, 60000);
    c.max_clipboard_item_bytes =
        std::clamp<std::uint64_t>(c.max_clipboard_item_bytes, 1024, 256ull * 1024 * 1024);
    c.retention_days = std::clamp(c.retention_days, 0, 3650);
    c.max_storage_bytes = std::clamp<std::uint64_t>(
        c.max_storage_bytes, 16ull * 1024 * 1024, 1024ull * 1024 * 1024 * 1024);
    c.snapshot_event_interval = std::clamp(c.snapshot_event_interval, 1, 100000);
    c.snapshot_time_interval_ms = std::clamp(c.snapshot_time_interval_ms, 1000, 3600000);
    if (c.toggle_hotkey.virtual_key == 0) {
        c.toggle_hotkey = Hotkey{true, true, false, false, 0x52};
    }
}

std::map<std::string, std::string> to_key_values(const Configuration& c) {
    std::map<std::string, std::string> kv;
    auto b = [](bool v) -> std::string { return v ? "true" : "false"; };

    kv["general.start_with_windows"] = b(c.start_with_windows);
    kv["general.start_minimized"] = b(c.start_minimized);
    kv["general.minimize_to_tray"] = b(c.minimize_to_tray);
    kv["general.close_to_tray"] = b(c.close_to_tray);
    kv["general.show_notifications"] = b(c.show_notifications);

    kv["recording.enable"] = b(c.enable_recording);
    kv["recording.keyboard"] = b(c.record_keyboard);
    kv["recording.mouse_clicks"] = b(c.record_mouse_clicks);
    kv["recording.mouse_wheel"] = b(c.record_mouse_wheel);
    kv["recording.mouse_movement"] = b(c.record_mouse_movement);
    kv["recording.track_active_window"] = b(c.track_active_window);

    kv["mouse.movement_sampling_ms"] = num_to_string(c.mouse_movement_sampling_ms);

    kv["clipboard.monitor"] = b(c.monitor_clipboard);
    kv["clipboard.record_text"] = b(c.record_text_clipboard);
    kv["clipboard.max_item_bytes"] = num_to_string(c.max_clipboard_item_bytes);

    kv["history.retention_days"] = num_to_string(c.retention_days);
    kv["history.max_storage_bytes"] = num_to_string(c.max_storage_bytes);
    kv["history.automatic_cleanup"] = b(c.automatic_cleanup);
    kv["history.create_snapshots"] = b(c.create_snapshots);
    kv["history.snapshot_event_interval"] = num_to_string(c.snapshot_event_interval);
    kv["history.snapshot_time_interval_ms"] = num_to_string(c.snapshot_time_interval_ms);

    kv["appearance.theme"] = std::string(to_string(c.theme));
    kv["window.always_on_top"] = b(c.always_on_top);
    kv["hotkey.toggle"] = c.toggle_hotkey.to_string();
    return kv;
}

Configuration configuration_from_key_values(const std::map<std::string, std::string>& kv) {
    Configuration c;  // start from defaults

    c.start_with_windows = map_bool(kv, "general.start_with_windows", c.start_with_windows);
    c.start_minimized = map_bool(kv, "general.start_minimized", c.start_minimized);
    c.minimize_to_tray = map_bool(kv, "general.minimize_to_tray", c.minimize_to_tray);
    c.close_to_tray = map_bool(kv, "general.close_to_tray", c.close_to_tray);
    c.show_notifications = map_bool(kv, "general.show_notifications", c.show_notifications);

    c.enable_recording = map_bool(kv, "recording.enable", c.enable_recording);
    c.record_keyboard = map_bool(kv, "recording.keyboard", c.record_keyboard);
    c.record_mouse_clicks = map_bool(kv, "recording.mouse_clicks", c.record_mouse_clicks);
    c.record_mouse_wheel = map_bool(kv, "recording.mouse_wheel", c.record_mouse_wheel);
    c.record_mouse_movement =
        map_bool(kv, "recording.mouse_movement", c.record_mouse_movement);
    c.track_active_window =
        map_bool(kv, "recording.track_active_window", c.track_active_window);

    c.mouse_movement_sampling_ms =
        map_i32(kv, "mouse.movement_sampling_ms", c.mouse_movement_sampling_ms);

    c.monitor_clipboard = map_bool(kv, "clipboard.monitor", c.monitor_clipboard);
    c.record_text_clipboard = map_bool(kv, "clipboard.record_text", c.record_text_clipboard);
    c.max_clipboard_item_bytes =
        map_u64(kv, "clipboard.max_item_bytes", c.max_clipboard_item_bytes);

    c.retention_days = map_i32(kv, "history.retention_days", c.retention_days);
    c.max_storage_bytes = map_u64(kv, "history.max_storage_bytes", c.max_storage_bytes);
    c.automatic_cleanup = map_bool(kv, "history.automatic_cleanup", c.automatic_cleanup);
    c.create_snapshots = map_bool(kv, "history.create_snapshots", c.create_snapshots);
    c.snapshot_event_interval =
        map_i32(kv, "history.snapshot_event_interval", c.snapshot_event_interval);
    c.snapshot_time_interval_ms =
        map_i32(kv, "history.snapshot_time_interval_ms", c.snapshot_time_interval_ms);

    if (auto it = kv.find("appearance.theme"); it != kv.end()) {
        c.theme = theme_from_string(it->second, c.theme);
    }
    c.always_on_top = map_bool(kv, "window.always_on_top", c.always_on_top);
    if (auto it = kv.find("hotkey.toggle"); it != kv.end()) {
        Hotkey hk;
        if (Hotkey::parse(it->second, hk)) c.toggle_hotkey = hk;
    }
    return c;
}

}  // namespace ir
