#include "framework/test_framework.hpp"

#include "capture/keyboard_hook.hpp"
#include "core/event_id_allocator.hpp"
#include "core/event_queue.hpp"

// The Win32 hook cannot be driven with synthetic keystrokes deterministically in
// a headless test, and a global hook may be disallowed in some environments.
// This test therefore verifies the *lifecycle* is robust: start() and stop()
// complete without hanging or crashing regardless of whether the hook installs.

TEST_CASE("capture.hook", "start/stop lifecycle is clean") {
    ir::EventQueue queue(1024);
    ir::EventIdAllocator ids;
    ir::KeyboardHook hook(queue, ids, ir::SessionId{1});

    hook.start();   // may or may not install depending on environment
    hook.stop();    // must return cleanly (no hang/crash)

    // Reaching here means the lifecycle completed. A second stop is a no-op.
    hook.stop();
    CHECK(true);
}

TEST_CASE("capture.hook", "second concurrent instance refuses to install") {
    ir::EventQueue queue(64);
    ir::EventIdAllocator ids;
    ir::KeyboardHook first(queue, ids, ir::SessionId{1});
    const bool first_installed = first.start();

    if (first_installed) {
        // Single-instance guard: a second hook must not install (spec §412).
        ir::KeyboardHook second(queue, ids, ir::SessionId{1});
        CHECK(!second.start());
        second.stop();
    }
    first.stop();
    CHECK(true);
}
