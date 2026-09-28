#include "framework/test_framework.hpp"

#include "mouse/mouse_processor.hpp"

namespace {

ir::Timestamp at(std::int64_t mono_ns) { return ir::Timestamp{0, mono_ns}; }

ir::RawMouseInput button(ir::MouseButton b, bool down, std::int32_t x = 10,
                         std::int32_t y = 20) {
    ir::RawMouseInput r;
    r.action = down ? ir::RawMouseAction::ButtonDown : ir::RawMouseAction::ButtonUp;
    r.button = b;
    r.x = x;
    r.y = y;
    return r;
}

ir::RawMouseInput move(std::int32_t x, std::int32_t y) {
    ir::RawMouseInput r;
    r.action = ir::RawMouseAction::Move;
    r.x = x;
    r.y = y;
    return r;
}

ir::RawMouseInput wheel(std::int32_t delta) {
    ir::RawMouseInput r;
    r.action = ir::RawMouseAction::Wheel;
    r.wheel_delta = delta;
    return r;
}

const ir::MouseData& mouse(const ir::Event& e) {
    return std::get<ir::MouseData>(e.payload);
}

constexpr std::int64_t ms(std::int64_t n) { return n * 1'000'000; }

}  // namespace

TEST_CASE("mouse.proc", "left button down/up produce MouseButton events") {
    ir::MouseProcessor p;  // defaults: buttons+wheel on, movement off
    auto down = p.process(button(ir::MouseButton::Left, true, 5, 7), at(1));
    REQUIRE(down.has_value());
    CHECK(down->type == ir::EventType::MouseButton);
    CHECK(down->category == ir::EventCategory::UserInput);
    CHECK(mouse(*down).button == ir::MouseButton::Left);
    CHECK(mouse(*down).pressed);
    CHECK_EQ(mouse(*down).x, 5);
    CHECK_EQ(mouse(*down).y, 7);

    auto up = p.process(button(ir::MouseButton::Left, false, 5, 7), at(2));
    REQUIRE(up.has_value());
    CHECK(up->type == ir::EventType::MouseButton);
    CHECK(!mouse(*up).pressed);
}

TEST_CASE("mouse.proc", "right and middle and X buttons carry the button id") {
    ir::MouseProcessor p;
    CHECK(mouse(*p.process(button(ir::MouseButton::Right, true), at(1))).button ==
          ir::MouseButton::Right);
    CHECK(mouse(*p.process(button(ir::MouseButton::Middle, true), at(2))).button ==
          ir::MouseButton::Middle);
    CHECK(mouse(*p.process(button(ir::MouseButton::X1, true), at(3))).button ==
          ir::MouseButton::X1);
    CHECK(mouse(*p.process(button(ir::MouseButton::X2, true), at(4))).button ==
          ir::MouseButton::X2);
}

TEST_CASE("mouse.proc", "wheel produces MouseWheel with signed delta") {
    ir::MouseProcessor p;
    auto up = p.process(wheel(120), at(1));
    REQUIRE(up.has_value());
    CHECK(up->type == ir::EventType::MouseWheel);
    CHECK_EQ(mouse(*up).wheel_delta, 120);
    CHECK(mouse(*up).button == ir::MouseButton::None);

    auto down = p.process(wheel(-240), at(2));
    REQUIRE(down.has_value());
    CHECK_EQ(mouse(*down).wheel_delta, -240);
}

TEST_CASE("mouse.proc", "movement is dropped when movement recording is off") {
    ir::MouseProcessor p;  // movement off by default
    CHECK(!p.process(move(1, 1), at(1)).has_value());
    CHECK(!p.process(move(2, 2), at(ms(1000))).has_value());
}

TEST_CASE("mouse.proc", "movement recorded and throttled to the sampling interval") {
    ir::MouseSettings s;
    s.record_movement = true;
    s.movement_sampling_ms = 100;
    ir::MouseProcessor p(s);

    // First move always recorded.
    auto first = p.process(move(0, 0), at(0));
    REQUIRE(first.has_value());
    CHECK(first->type == ir::EventType::MouseMove);
    CHECK(mouse(*first).button == ir::MouseButton::None);

    // 50 ms later: within the 100 ms window -> throttled.
    CHECK(!p.process(move(1, 1), at(ms(50))).has_value());

    // Exactly 100 ms after the last recorded sample -> recorded.
    auto third = p.process(move(2, 2), at(ms(100)));
    REQUIRE(third.has_value());
    CHECK_EQ(mouse(*third).x, 2);

    // 40 ms after that (140 total) -> throttled again.
    CHECK(!p.process(move(3, 3), at(ms(140))).has_value());
    // 100 ms after the last recorded (200 total) -> recorded.
    CHECK(p.process(move(4, 4), at(ms(200))).has_value());
}

TEST_CASE("mouse.proc", "zero sampling interval records every movement sample") {
    ir::MouseSettings s;
    s.record_movement = true;
    s.movement_sampling_ms = 0;
    ir::MouseProcessor p(s);
    CHECK(p.process(move(0, 0), at(0)).has_value());
    CHECK(p.process(move(1, 1), at(1)).has_value());
    CHECK(p.process(move(2, 2), at(2)).has_value());
}

TEST_CASE("mouse.proc", "reset clears throttle so the next move is recorded") {
    ir::MouseSettings s;
    s.record_movement = true;
    s.movement_sampling_ms = 100;
    ir::MouseProcessor p(s);
    CHECK(p.process(move(0, 0), at(0)).has_value());
    CHECK(!p.process(move(1, 1), at(ms(10))).has_value());  // throttled
    p.reset();
    CHECK(p.process(move(2, 2), at(ms(20))).has_value());  // first after reset
}

TEST_CASE("mouse.proc", "button recording toggle suppresses button events") {
    ir::MouseSettings s;
    s.record_buttons = false;
    ir::MouseProcessor p(s);
    CHECK(!p.process(button(ir::MouseButton::Left, true), at(1)).has_value());
}

TEST_CASE("mouse.proc", "wheel recording toggle suppresses wheel events") {
    ir::MouseSettings s;
    s.record_wheel = false;
    ir::MouseProcessor p(s);
    CHECK(!p.process(wheel(120), at(1)).has_value());
}

TEST_CASE("mouse.proc", "injected flag propagates") {
    ir::MouseProcessor p;
    ir::RawMouseInput r = button(ir::MouseButton::Left, true);
    r.injected = true;
    auto e = p.process(r, at(1));
    REQUIRE(e.has_value());
    CHECK(ir::has_flag(e->flags, ir::EventFlags::Injected));
}

TEST_CASE("mouse.proc", "settings can be changed at runtime") {
    ir::MouseProcessor p;
    CHECK(!p.process(move(0, 0), at(0)).has_value());  // movement off
    ir::MouseSettings s;
    s.record_movement = true;
    s.movement_sampling_ms = 0;
    p.set_settings(s);
    CHECK(p.process(move(1, 1), at(1)).has_value());
}
