#include "framework/test_framework.hpp"

#include "ui/tray_menu_model.hpp"

TEST_CASE("ui.tray", "recording state drives tooltip and toggle label") {
    ir::TrayMenu m = ir::tray_menu(ir::RecordingStatus::Recording, true);
    CHECK(m.tooltip.find("Recording") != std::string::npos);
    CHECK_EQ(m.toggle_label, std::string("Pause recording"));

    ir::TrayMenu p = ir::tray_menu(ir::RecordingStatus::Paused, true);
    CHECK(p.tooltip.find("Paused") != std::string::npos);
    CHECK_EQ(p.toggle_label, std::string("Resume recording"));
}

TEST_CASE("ui.tray", "show/hide label reflects window visibility") {
    CHECK_EQ(ir::tray_menu(ir::RecordingStatus::Recording, true).show_hide_label,
             std::string("Hide window"));
    CHECK_EQ(ir::tray_menu(ir::RecordingStatus::Recording, false).show_hide_label,
             std::string("Show window"));
}
