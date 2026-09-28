#include "framework/test_framework.hpp"

#include "ui/theme.hpp"

TEST_CASE("ui.theme", "effective_dark resolves the preference") {
    CHECK(!ir::effective_dark(ir::Theme::Light, true));
    CHECK(ir::effective_dark(ir::Theme::Dark, false));
    CHECK(ir::effective_dark(ir::Theme::System, true));
    CHECK(!ir::effective_dark(ir::Theme::System, false));
}

TEST_CASE("ui.theme", "dark and light palettes differ and are internally distinct") {
    ir::ThemeColors dark = ir::theme_colors(true);
    ir::ThemeColors light = ir::theme_colors(false);
    CHECK(dark.window_bg != light.window_bg);
    CHECK(dark.text != light.text);
    // Dark background should be darker than light background.
    CHECK(dark.window_bg < light.window_bg);
    // Text must contrast with the background within each palette.
    CHECK(dark.text != dark.window_bg);
    CHECK(light.text != light.window_bg);
}
