#include "framework/test_framework.hpp"

#include "history/snapshot_policy.hpp"

TEST_CASE("history.snap_policy", "disabled policy never fires") {
    ir::SnapshotSettings s;
    s.enabled = false;
    s.event_interval = 1;
    ir::SnapshotPolicy p(s);
    for (int i = 0; i < 10; ++i) CHECK(!p.on_event(i));
}

TEST_CASE("history.snap_policy", "fires after the event interval and resets") {
    ir::SnapshotSettings s;
    s.event_interval = 3;
    s.time_interval_ms = 0;  // event count only
    ir::SnapshotPolicy p(s);
    CHECK(!p.on_event(0));
    CHECK(!p.on_event(0));
    CHECK(p.on_event(0));  // 3rd event
    p.note_snapshot(0);
    CHECK(!p.on_event(0));
    CHECK(!p.on_event(0));
    CHECK(p.on_event(0));  // 3 more
}

TEST_CASE("history.snap_policy", "fires on the time interval before the event count") {
    ir::SnapshotSettings s;
    s.event_interval = 1000;   // effectively never by count
    s.time_interval_ms = 500;
    ir::SnapshotPolicy p(s);
    CHECK(!p.on_event(0));      // baseline
    CHECK(!p.on_event(100));
    CHECK(p.on_event(600));     // 600 ms since baseline
}

TEST_CASE("history.snap_policy", "both intervals disabled means it never fires") {
    ir::SnapshotSettings s;
    s.event_interval = 0;
    s.time_interval_ms = 0;
    ir::SnapshotPolicy p(s);
    for (int i = 0; i < 5; ++i) CHECK(!p.on_event(i * 100000));
}

TEST_CASE("history.snap_policy", "reset clears counters") {
    ir::SnapshotSettings s;
    s.event_interval = 2;
    s.time_interval_ms = 0;
    ir::SnapshotPolicy p(s);
    CHECK(!p.on_event(0));
    p.reset();
    CHECK(!p.on_event(0));  // would have fired without reset
    CHECK(p.on_event(0));
}
