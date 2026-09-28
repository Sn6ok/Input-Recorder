#include "framework/test_framework.hpp"

#include "ui/app_view_model.hpp"

TEST_CASE("ui.viewmodel", "recording indicator reflects status") {
    CHECK_EQ(ir::status_indicator(ir::RecordingStatus::Recording),
             std::string("\xE2\x97\x8F RECORDING"));
    CHECK_EQ(ir::status_indicator(ir::RecordingStatus::Paused),
             std::string("\xE2\x97\x8B PAUSED"));
    CHECK(ir::is_recording(ir::RecordingStatus::Recording));
    CHECK(!ir::is_recording(ir::RecordingStatus::Paused));
    CHECK(ir::toggled(ir::RecordingStatus::Recording) == ir::RecordingStatus::Paused);
}

TEST_CASE("ui.viewmodel", "default status is recording; toggle flips it") {
    ir::AppViewModel vm;
    CHECK(vm.status() == ir::RecordingStatus::Recording);
    CHECK(vm.status_indicator() == std::string("\xE2\x97\x8F RECORDING"));
    CHECK(vm.toggle_status() == ir::RecordingStatus::Paused);
    CHECK(vm.status_indicator() == std::string("\xE2\x97\x8B PAUSED"));
    vm.set_status(ir::RecordingStatus::Recording);
    CHECK(vm.status() == ir::RecordingStatus::Recording);
}

TEST_CASE("ui.viewmodel", "empty state shows a placeholder, not blank") {
    ir::AppViewModel vm;
    CHECK(!vm.has_text());
    CHECK(!vm.display_text().empty());
    CHECK(vm.copy_all_text().empty());  // Copy All copies nothing when empty
}

TEST_CASE("ui.viewmodel", "reconstruction text is shown and copied verbatim") {
    ir::AppViewModel vm;
    vm.set_reconstruction("Hello World", ir::Confidence::High);
    CHECK(vm.has_text());
    CHECK_EQ(vm.display_text(), std::string("Hello World"));
    CHECK_EQ(vm.copy_all_text(), std::string("Hello World"));
    CHECK(vm.confidence() == ir::Confidence::High);
}

TEST_CASE("ui.viewmodel", "confidence note only appears below High") {
    ir::AppViewModel vm;
    vm.set_reconstruction("x", ir::Confidence::High);
    CHECK(vm.confidence_note().empty());
    vm.set_reconstruction("x", ir::Confidence::Medium);
    CHECK(!vm.confidence_note().empty());
    vm.set_reconstruction("x", ir::Confidence::Low);
    CHECK(!vm.confidence_note().empty());
    vm.set_reconstruction("x", ir::Confidence::Unknown);
    CHECK(!vm.confidence_note().empty());
}
