#include "framework/test_framework.hpp"

#include "keyboard/keyboard_translator.hpp"

namespace {

constexpr std::uint16_t kA = 0x41;
constexpr std::uint16_t kC = 0x43;
constexpr std::uint16_t kLShift = 0xA0;
constexpr std::uint16_t kLControl = 0xA2;

ir::Timestamp ts() { return ir::Timestamp{1000, 2000}; }

ir::RawKeyboardInput down(std::uint16_t vk, bool ext = false, bool inj = false) {
    return ir::RawKeyboardInput{vk, 0, false, ext, inj};
}
ir::RawKeyboardInput up(std::uint16_t vk) {
    return ir::RawKeyboardInput{vk, 0, true, false, false};
}

const ir::KeyEventData& key(const ir::Event& e) {
    return std::get<ir::KeyEventData>(e.payload);
}

}  // namespace

TEST_CASE("capture.keyboard", "KeyDown then KeyUp classified distinctly") {
    ir::KeyboardTranslator t;
    ir::Event d = t.translate(down(kA), ts());
    CHECK(d.type == ir::EventType::KeyDown);
    CHECK(d.category == ir::EventCategory::UserInput);
    CHECK_EQ(key(d).virtual_key, kA);
    CHECK(!ir::has_flag(d.flags, ir::EventFlags::KeyRepeat));

    ir::Event u = t.translate(up(kA), ts());
    CHECK(u.type == ir::EventType::KeyUp);
}

TEST_CASE("capture.keyboard", "auto-repeat detected on second KeyDown") {
    ir::KeyboardTranslator t;
    ir::Event first = t.translate(down(kA), ts());
    ir::Event second = t.translate(down(kA), ts());  // held down
    CHECK(!ir::has_flag(first.flags, ir::EventFlags::KeyRepeat));
    CHECK(ir::has_flag(second.flags, ir::EventFlags::KeyRepeat));

    t.translate(up(kA), ts());
    ir::Event third = t.translate(down(kA), ts());  // pressed again after release
    CHECK(!ir::has_flag(third.flags, ir::EventFlags::KeyRepeat));
}

TEST_CASE("capture.keyboard", "modifier state tracked across keys") {
    ir::KeyboardTranslator t;
    ir::Event shift_down = t.translate(down(kLShift), ts());
    CHECK(ir::has_flag(shift_down.flags, ir::EventFlags::ModShift));

    ir::Event a_down = t.translate(down(kA), ts());
    CHECK(ir::has_flag(a_down.flags, ir::EventFlags::ModShift));

    ir::Event shift_up = t.translate(up(kLShift), ts());
    CHECK(!ir::has_flag(shift_up.flags, ir::EventFlags::ModShift));

    ir::Event a_again = t.translate(down(kA), ts());
    CHECK(!ir::has_flag(a_again.flags, ir::EventFlags::ModShift));
}

TEST_CASE("capture.keyboard", "Ctrl+C carries ModCtrl on the letter") {
    ir::KeyboardTranslator t;
    ir::Event ctrl = t.translate(down(kLControl), ts());
    CHECK(ir::has_flag(ctrl.flags, ir::EventFlags::ModCtrl));
    ir::Event c = t.translate(down(kC), ts());
    CHECK(c.type == ir::EventType::KeyDown);
    CHECK(ir::has_flag(c.flags, ir::EventFlags::ModCtrl));
    CHECK_EQ(key(c).virtual_key, kC);
}

TEST_CASE("capture.keyboard", "injected and extended flags propagate") {
    ir::KeyboardTranslator t;
    ir::Event inj = t.translate(down(kA, /*ext=*/false, /*inj=*/true), ts());
    CHECK(ir::has_flag(inj.flags, ir::EventFlags::Injected));
    CHECK(!ir::has_flag(inj.flags, ir::EventFlags::Extended));

    ir::KeyboardTranslator t2;
    ir::Event ext = t2.translate(down(0x25 /*VK_LEFT*/, /*ext=*/true), ts());
    CHECK(ir::has_flag(ext.flags, ir::EventFlags::Extended));
}

TEST_CASE("capture.keyboard", "reset clears modifier state") {
    ir::KeyboardTranslator t;
    t.translate(down(kLControl), ts());
    CHECK(ir::has_flag(t.modifier_flags(), ir::EventFlags::ModCtrl));
    t.reset();
    CHECK(!ir::has_flag(t.modifier_flags(), ir::EventFlags::ModCtrl));
    CHECK(!t.is_down(kLControl));
}
