#include "framework/test_framework.hpp"

#include "clipboard/clipboard_monitor.hpp"
#include "core/event_id_allocator.hpp"
#include "core/event_queue.hpp"

// The Win32 clipboard monitor is a thin wrapper; its semantics (dedup,
// truncation, self-copy, paste) are covered deterministically by the
// ClipboardProcessor tests. Here we only verify the lifecycle is robust.

TEST_CASE("clipboard.monitor", "start/stop lifecycle is clean") {
    ir::EventQueue queue(256);
    ir::EventIdAllocator ids;
    ir::ClipboardMonitor monitor(queue, ids, ir::SessionId{1}, 1u << 20);

    monitor.start();  // installs a message-only window + format listener
    monitor.note_self_copy("anything");  // thread-safe, no-op if idle
    monitor.stop();   // must return cleanly
    monitor.stop();   // idempotent
    CHECK(true);
}
