#include "framework/test_framework.hpp"

#include "core/configuration.hpp"

TEST_CASE("core.config", "defaults match the spec") {
    ir::Configuration c;
    // Recording defaults (spec §254)
    CHECK(c.enable_recording);
    CHECK(c.record_keyboard);
    CHECK(c.record_mouse_clicks);
    CHECK(c.record_mouse_wheel);
    CHECK(!c.record_mouse_movement);  // OFF by default (spec §255)
    CHECK(c.track_active_window);
    CHECK(c.record_text_clipboard);
    // Appearance default System (spec §258)
    CHECK(c.theme == ir::Theme::System);
    // Always on Top off by default (spec §654)
    CHECK(!c.always_on_top);
    // Toggle hotkey Ctrl+Shift+R (spec §88)
    CHECK_EQ(c.toggle_hotkey.to_string(), std::string("Ctrl+Shift+R"));
}

TEST_CASE("core.config", "key/value round-trip preserves configuration") {
    ir::Configuration c;
    c.record_mouse_movement = true;
    c.theme = ir::Theme::Dark;
    c.always_on_top = true;
    c.retention_days = 30;
    c.max_clipboard_item_bytes = 4096;
    c.toggle_hotkey = ir::Hotkey{true, false, true, false, 'K'};

    auto kv = ir::to_key_values(c);
    ir::Configuration restored = ir::configuration_from_key_values(kv);
    CHECK(restored == c);
}

TEST_CASE("core.config", "unknown keys ignored, missing keys keep defaults") {
    std::map<std::string, std::string> kv;
    kv["appearance.theme"] = "Light";
    kv["totally.unknown"] = "whatever";
    ir::Configuration c = ir::configuration_from_key_values(kv);
    CHECK(c.theme == ir::Theme::Light);
    CHECK(c.enable_recording);  // default retained
}

TEST_CASE("core.config", "validate clamps out-of-range values") {
    ir::Configuration c;
    c.mouse_movement_sampling_ms = -5;
    c.retention_days = 100000;
    c.max_clipboard_item_bytes = 10;      // below minimum
    c.snapshot_time_interval_ms = 5;      // below minimum
    ir::validate(c);
    CHECK(c.mouse_movement_sampling_ms >= 10);
    CHECK(c.retention_days <= 3650);
    CHECK(c.max_clipboard_item_bytes >= 1024);
    CHECK(c.snapshot_time_interval_ms >= 1000);
}

TEST_CASE("core.config", "hotkey parse round-trips common forms") {
    ir::Hotkey hk;
    CHECK(ir::Hotkey::parse("Ctrl+Shift+R", hk));
    CHECK(hk.ctrl);
    CHECK(hk.shift);
    CHECK(!hk.alt);
    CHECK_EQ(hk.virtual_key, static_cast<std::uint16_t>('R'));

    CHECK(ir::Hotkey::parse("Alt+F4", hk));
    CHECK(hk.alt);
    CHECK_EQ(hk.virtual_key, static_cast<std::uint16_t>(0x73));  // VK_F4

    CHECK(!ir::Hotkey::parse("Ctrl+Shift", hk));  // no non-modifier key
}
