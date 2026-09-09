#include "framework/test_framework.hpp"

#include "core/clipboard_entry.hpp"
#include "core/context.hpp"
#include "core/event.hpp"
#include "core/session.hpp"

TEST_CASE("core.session", "lifecycle status transitions") {
    ir::Session s;
    s.id = ir::SessionId{1};
    s.started_at_ms = 1000;
    CHECK(s.status == ir::SessionStatus::Active);
    CHECK(!s.ended_at_ms.has_value());

    s.ended_at_ms = 2000;
    s.status = ir::SessionStatus::Completed;
    CHECK(s.ended_at_ms.has_value());
    CHECK_EQ(*s.ended_at_ms, 2000LL);
    CHECK_EQ(std::string(ir::to_string(s.status)), std::string("Completed"));
}

TEST_CASE("core.context", "same_identity_as ignores title changes") {
    ir::Context a;
    a.process_id = 1234;
    a.window_handle = 0xABCD;
    a.process_name = "claude.exe";
    a.window_title = "Claude";

    ir::Context b = a;
    b.window_title = "Claude - new chat";  // title changed only
    CHECK(a.same_identity_as(b));

    ir::Context c = a;
    c.window_handle = 0x9999;  // different window
    CHECK(!a.same_identity_as(c));
}

TEST_CASE("core.clipboard", "fnv1a hash detects identical vs different content") {
    const std::uint64_t h1 = ir::fnv1a_64("Create a C++ project");
    const std::uint64_t h2 = ir::fnv1a_64("Create a C++ project");
    const std::uint64_t h3 = ir::fnv1a_64("Create a C++ project!");
    CHECK_EQ(h1, h2);
    CHECK_NE(h1, h3);
}

TEST_CASE("core.event", "ordering uses monotonic time then id") {
    ir::Event a;
    a.id = ir::EventId{100};
    a.time.monotonic_ns = 500;
    ir::Event b;
    b.id = ir::EventId{101};
    b.time.monotonic_ns = 500;  // same timestamp
    CHECK(ir::ordered_before(a, b));   // tiebreak by id
    CHECK(!ir::ordered_before(b, a));

    ir::Event c;
    c.id = ir::EventId{1};
    c.time.monotonic_ns = 400;
    CHECK(ir::ordered_before(c, a));   // earlier monotonic wins despite lower id
}

TEST_CASE("core.event.flags", "bitwise flag helpers") {
    ir::EventFlags f = ir::EventFlags::Injected | ir::EventFlags::KeyRepeat;
    CHECK(ir::has_flag(f, ir::EventFlags::Injected));
    CHECK(ir::has_flag(f, ir::EventFlags::KeyRepeat));
    CHECK(!ir::has_flag(f, ir::EventFlags::ModAlt));
}
