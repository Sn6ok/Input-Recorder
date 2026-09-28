#include "framework/test_framework.hpp"

#include <string>

#include "clipboard/clipboard_processor.hpp"
#include "core/event_id_allocator.hpp"
#include "reconstruction/reconstruction_engine.hpp"

namespace {
ir::ContextId ctx0{0};
}

TEST_CASE("clipboard.proc", "external copy produces an entry and an event") {
    ir::EventIdAllocator ids;
    ir::ClipboardProcessor p(ids, ir::SessionId{1}, 1u << 20);
    auto r = p.on_clipboard_text("Hello World", 1000, 2000, ctx0);
    CHECK(!r.suppressed);
    REQUIRE(r.entry.has_value());
    REQUIRE(r.event.has_value());
    CHECK_EQ(r.entry->text, std::string("Hello World"));
    CHECK(!r.entry->truncated);
    CHECK(r.event->type == ir::EventType::ClipboardChanged);
    CHECK(r.event->category == ir::EventCategory::ClipboardEvent);
    // Event references the entry.
    const auto& ref = std::get<ir::ClipboardRef>(r.event->payload);
    CHECK(ref.entry == r.entry->id);
}

TEST_CASE("clipboard.proc", "identical consecutive content is suppressed") {
    ir::EventIdAllocator ids;
    ir::ClipboardProcessor p(ids, ir::SessionId{1}, 1u << 20);
    auto r1 = p.on_clipboard_text("same", 1, 1, ctx0);
    CHECK(!r1.suppressed);
    auto r2 = p.on_clipboard_text("same", 2, 2, ctx0);
    CHECK(r2.suppressed);
    CHECK(!r2.entry.has_value());
    // A different value is recorded again.
    auto r3 = p.on_clipboard_text("different", 3, 3, ctx0);
    CHECK(!r3.suppressed);
}

TEST_CASE("clipboard.proc", "internal Copy All is not recorded as a user event") {
    ir::EventIdAllocator ids;
    ir::ClipboardProcessor p(ids, ir::SessionId{1}, 1u << 20);
    p.note_self_copy("recovered text copied by the app");
    auto r = p.on_clipboard_text("recovered text copied by the app", 1, 1, ctx0);
    CHECK(r.suppressed);
    CHECK(!r.event.has_value());
    // A subsequent genuine external copy is still recorded.
    auto r2 = p.on_clipboard_text("something the user copied", 2, 2, ctx0);
    CHECK(!r2.suppressed);
}

TEST_CASE("clipboard.proc", "oversized clipboard text is truncated and flagged") {
    ir::EventIdAllocator ids;
    ir::ClipboardProcessor p(ids, ir::SessionId{1}, /*max_bytes=*/16);
    const std::string big(1000, 'x');
    auto r = p.on_clipboard_text(big, 1, 1, ctx0);
    REQUIRE(r.entry.has_value());
    CHECK(r.entry->truncated);
    CHECK_EQ(r.entry->text.size(), static_cast<std::size_t>(16));
    CHECK_EQ(r.entry->original_size_bytes, static_cast<std::uint64_t>(1000));
    REQUIRE(r.event.has_value());
    CHECK(ir::has_flag(r.event->flags, ir::EventFlags::Truncated));
}

TEST_CASE("clipboard.proc", "truncation never splits a multi-byte code point") {
    ir::EventIdAllocator ids;
    // Emoji is 4 bytes; cap at 5 must keep only 1 emoji (4 bytes), not a partial.
    ir::ClipboardProcessor p(ids, ir::SessionId{1}, /*max_bytes=*/5);
    const std::string emoji = "\xF0\x9F\x98\x80\xF0\x9F\x98\x80";  // two 😀
    auto r = p.on_clipboard_text(emoji, 1, 1, ctx0);
    REQUIRE(r.entry.has_value());
    CHECK_EQ(r.entry->text, std::string("\xF0\x9F\x98\x80"));  // exactly one emoji
    CHECK(r.entry->truncated);
}

TEST_CASE("clipboard.proc", "paste event carries text and reconstruction inserts it") {
    ir::EventIdAllocator ids;
    ir::ClipboardProcessor p(ids, ir::SessionId{1}, 1u << 20);
    ir::Event paste = p.make_paste_event("Hello World", 10, 20, ctx0);
    CHECK(paste.type == ir::EventType::Paste);

    ir::ReconstructionEngine engine;
    engine.process(paste);
    CHECK_EQ(engine.text(), std::string("Hello World"));  // spec §496
}
