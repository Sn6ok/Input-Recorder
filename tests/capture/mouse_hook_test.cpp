#include "framework/test_framework.hpp"

#include "capture/mouse_hook.hpp"
#include "core/event_id_allocator.hpp"
#include "core/event_queue.hpp"

// The Win32 hook cannot be driven with synthetic mouse input deterministically
// in a headless test, and a global hook may be disallowed in some environments.
// This test therefore verifies the *lifecycle* is robust: start()/stop()
// complete without hanging or crashing regardless of whether the hook installs.
// The event semantics (button/wheel/movement throttling) are covered
// deterministically by the MouseProcessor tests.

TEST_CASE("capture.mouse_hook", "start/stop lifecycle is clean") {
    ir::EventQueue queue(1024);
    ir::EventIdAllocator ids;
    ir::MouseHook hook(queue, ids, ir::SessionId{1});

    hook.start();  // may or may not install depending on environment
    hook.stop();   // must return cleanly (no hang/crash)
    hook.stop();   // idempotent
    CHECK(true);
}

TEST_CASE("capture.mouse_hook", "second concurrent instance refuses to install") {
    ir::EventQueue queue(64);
    ir::EventIdAllocator ids;
    ir::MouseHook first(queue, ids, ir::SessionId{1});
    const bool first_installed = first.start();

    if (first_installed) {
        ir::MouseHook second(queue, ids, ir::SessionId{1});
        CHECK(!second.start());  // single-instance guard (spec §412)
        second.stop();
    }
    first.stop();
    CHECK(true);
}

TEST_CASE("capture.mouse_hook", "settings can be updated before and after start") {
    ir::EventQueue queue(64);
    ir::EventIdAllocator ids;
    ir::MouseSettings s;
    s.record_movement = true;
    ir::MouseHook hook(queue, ids, ir::SessionId{1}, s);

    ir::MouseSettings s2;
    s2.record_movement = false;
    hook.set_settings(s2);  // thread-safe, no-op if idle
    hook.start();
    hook.set_settings(s);
    hook.stop();
    CHECK(true);
}
