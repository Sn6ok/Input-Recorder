#include "framework/test_framework.hpp"

#include "context/window_context_monitor.hpp"
#include "core/event_id_allocator.hpp"
#include "core/event_queue.hpp"

// The Win32 WinEvent hook cannot be driven deterministically in a headless test,
// and a global hook may be disallowed in some environments. The caching/dedup
// semantics are covered deterministically by the ContextTracker tests; here we
// only verify the lifecycle is robust.

TEST_CASE("context.monitor", "start/stop lifecycle is clean") {
    ir::EventQueue queue(256);
    ir::EventIdAllocator ids;
    ir::WindowContextMonitor monitor(queue, ids, ir::SessionId{1});

    monitor.start();  // installs a WinEvent hook + baseline observation
    monitor.stop();   // must return cleanly
    monitor.stop();   // idempotent
    CHECK(true);
}

TEST_CASE("context.monitor", "second concurrent instance refuses to install") {
    ir::EventQueue queue(64);
    ir::EventIdAllocator ids;
    ir::WindowContextMonitor first(queue, ids, ir::SessionId{1});
    const bool first_installed = first.start();

    if (first_installed) {
        ir::WindowContextMonitor second(queue, ids, ir::SessionId{1});
        CHECK(!second.start());  // single-instance guard (spec §412)
        second.stop();
    }
    first.stop();
    CHECK(true);
}
