#include "framework/test_framework.hpp"

#include "context/context_tracker.hpp"

namespace {

ir::WindowObservation obs(std::string proc, std::string title,
                          std::uint32_t pid, std::uint64_t hwnd,
                          std::int64_t wall = 0) {
    ir::WindowObservation o;
    o.process_name = std::move(proc);
    o.window_title = std::move(title);
    o.process_id = pid;
    o.window_handle = hwnd;
    o.wall_ms = wall;
    return o;
}

}  // namespace

TEST_CASE("context.tracker", "first observation allocates and reports a change") {
    ir::ContextTracker t{ir::SessionId{7}};
    auto u = t.observe(obs("code.exe", "main.cpp", 100, 0xA, 1000));
    CHECK(u.changed);
    CHECK(u.is_new);
    CHECK_EQ(u.context.id.value, 1u);
    CHECK_EQ(u.context.session.value, 7u);
    CHECK_EQ(u.context.process_name, std::string("code.exe"));
    CHECK_EQ(u.context.window_title, std::string("main.cpp"));
    CHECK_EQ(u.context.first_seen_ms, 1000);
    CHECK_EQ(u.context.last_seen_ms, 1000);
    CHECK_EQ(t.size(), 1u);
    CHECK(t.current() == u.context.id);
}

TEST_CASE("context.tracker", "re-observing the active window produces no change") {
    ir::ContextTracker t{ir::SessionId{1}};
    t.observe(obs("code.exe", "main.cpp", 100, 0xA, 1000));
    auto again = t.observe(obs("code.exe", "main.cpp", 100, 0xA, 2500));
    CHECK(!again.changed);
    CHECK(!again.is_new);
    CHECK_EQ(again.context.id.value, 1u);
    CHECK_EQ(again.context.first_seen_ms, 1000);  // unchanged
    CHECK_EQ(again.context.last_seen_ms, 2500);   // refreshed
    CHECK_EQ(t.size(), 1u);
}

TEST_CASE("context.tracker", "switching windows allocates a second context") {
    ir::ContextTracker t{ir::SessionId{1}};
    t.observe(obs("code.exe", "main.cpp", 100, 0xA, 1000));
    auto b = t.observe(obs("chrome.exe", "Docs", 200, 0xB, 1100));
    CHECK(b.changed);
    CHECK(b.is_new);
    CHECK_EQ(b.context.id.value, 2u);
    CHECK_EQ(t.size(), 2u);
}

TEST_CASE("context.tracker", "returning to a prior window reuses its id (dedup)") {
    ir::ContextTracker t{ir::SessionId{1}};
    auto a = t.observe(obs("code.exe", "main.cpp", 100, 0xA, 1000));
    t.observe(obs("chrome.exe", "Docs", 200, 0xB, 1100));
    auto back = t.observe(obs("code.exe", "main.cpp", 100, 0xA, 1200));
    CHECK(back.changed);       // active context changed back...
    CHECK(!back.is_new);       // ...but no new context was allocated (cached)
    CHECK_EQ(back.context.id.value, a.context.id.value);
    CHECK_EQ(back.context.last_seen_ms, 1200);
    CHECK_EQ(t.size(), 2u);    // still only two distinct contexts
}

TEST_CASE("context.tracker", "title change on the same window is a distinct context") {
    ir::ContextTracker t{ir::SessionId{1}};
    auto first = t.observe(obs("code.exe", "main.cpp", 100, 0xA, 1000));
    auto retitled = t.observe(obs("code.exe", "other.cpp", 100, 0xA, 1050));
    CHECK(retitled.changed);
    CHECK(retitled.is_new);  // fidelity: history keeps what the window said
    CHECK_NE(retitled.context.id.value, first.context.id.value);
    CHECK_EQ(retitled.context.window_title, std::string("other.cpp"));
    CHECK_EQ(t.size(), 2u);
}

TEST_CASE("context.tracker", "distinct windows with the same title stay distinct") {
    ir::ContextTracker t{ir::SessionId{1}};
    t.observe(obs("notepad.exe", "Untitled", 10, 0x1, 1));
    auto second = t.observe(obs("notepad.exe", "Untitled", 20, 0x2, 2));
    CHECK(second.is_new);
    CHECK_EQ(t.size(), 2u);
}

TEST_CASE("context.tracker", "make_context_changed_event carries the context") {
    ir::ContextTracker t{ir::SessionId{9}};
    auto u = t.observe(obs("code.exe", "main.cpp", 100, 0xA, 1000));
    ir::Event e = t.make_context_changed_event(u, ir::Timestamp{1000, 2000});
    CHECK(e.type == ir::EventType::ContextChanged);
    CHECK(e.category == ir::EventCategory::ContextEvent);
    CHECK(e.context == u.context.id);
    CHECK_EQ(e.session.value, 9u);
    CHECK(std::holds_alternative<std::monostate>(e.payload));
}

TEST_CASE("context.tracker", "find returns stored records; unknown ids are null") {
    ir::ContextTracker t{ir::SessionId{1}};
    auto u = t.observe(obs("code.exe", "main.cpp", 100, 0xA, 1000));
    const ir::Context* c = t.find(u.context.id);
    REQUIRE(c != nullptr);
    CHECK_EQ(c->process_name, std::string("code.exe"));
    CHECK(t.find(ir::ContextId{999}) == nullptr);
}

TEST_CASE("context.tracker", "reset clears the cache") {
    ir::ContextTracker t{ir::SessionId{1}};
    t.observe(obs("code.exe", "main.cpp", 100, 0xA, 1000));
    t.observe(obs("chrome.exe", "Docs", 200, 0xB, 1100));
    CHECK_EQ(t.size(), 2u);
    t.reset();
    CHECK_EQ(t.size(), 0u);
    CHECK(!t.current().valid());
    // Ids restart from 1 after reset.
    auto u = t.observe(obs("code.exe", "main.cpp", 100, 0xA, 2000));
    CHECK_EQ(u.context.id.value, 1u);
}
