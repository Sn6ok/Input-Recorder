#include "framework/test_framework.hpp"

#include <string>

#include "ui/history_formatting.hpp"

namespace {

ir::Event text_ev(std::string text, std::int64_t wall) {
    ir::Event e;
    e.type = ir::EventType::TextInput;
    e.time.wall_ms = wall;
    e.payload = ir::TextInputData{std::move(text)};
    return e;
}

}  // namespace

TEST_CASE("ui.format", "truncate keeps short text and marks long text") {
    CHECK_EQ(ir::truncate_for_display("hello", 80), std::string("hello"));
    std::string longtext(200, 'a');
    std::string out = ir::truncate_for_display(longtext, 10);
    // 10 'a' + ellipsis (U+2026, 3 UTF-8 bytes).
    CHECK_EQ(out, std::string(10, 'a') + "\xE2\x80\xA6");
}

TEST_CASE("ui.format", "truncate never splits a multi-byte code point") {
    // 5 emoji (4 bytes each); limit 3 code points -> 3 emoji + ellipsis.
    std::string five;
    for (int i = 0; i < 5; ++i) five += "\xF0\x9F\x98\x80";  // U+1F600
    std::string out = ir::truncate_for_display(five, 3);
    std::string expect;
    for (int i = 0; i < 3; ++i) expect += "\xF0\x9F\x98\x80";
    expect += "\xE2\x80\xA6";
    CHECK_EQ(out, expect);
}

TEST_CASE("ui.format", "control characters collapse to spaces") {
    CHECK_EQ(ir::truncate_for_display("a\nb\tc\r d", 80), std::string("a b c  d"));
}

TEST_CASE("ui.format", "timestamp formats as UTC without milliseconds") {
    CHECK_EQ(ir::format_timestamp(0), std::string("1970-01-01 00:00:00"));
}

TEST_CASE("ui.format", "event rows describe their type") {
    CHECK_EQ(ir::format_event_row(text_ev("hi there", 0)),
             std::string("1970-01-01 00:00:00  typed: hi there"));

    ir::Event sc;
    sc.type = ir::EventType::KeyboardShortcut;
    sc.payload = ir::ShortcutData{0, 0x43, "Ctrl+C"};
    CHECK(ir::format_event_row(sc).find("shortcut: Ctrl+C") != std::string::npos);

    ir::Event mb;
    mb.type = ir::EventType::MouseButton;
    mb.payload = ir::MouseData{ir::MouseButton::Left, true, 0, 0, 0};
    CHECK(ir::format_event_row(mb).find("Left button down") != std::string::npos);
}

TEST_CASE("ui.format", "event row text is sanitized to one line") {
    std::string row = ir::format_event_row(text_ev("line1\nline2", 0));
    CHECK(row.find('\n') == std::string::npos);
    CHECK(row.find("line1 line2") != std::string::npos);
}

TEST_CASE("ui.format", "clipboard row marks truncation") {
    ir::ClipboardEntry e;
    e.timestamp_ms = 0;
    e.text = "copied text";
    e.truncated = true;
    std::string row = ir::format_clipboard_row(e);
    CHECK(row.find("clip: copied text") != std::string::npos);
    CHECK(row.find("(truncated)") != std::string::npos);
}

TEST_CASE("ui.format", "session row shows id and status") {
    ir::Session s;
    s.id = ir::SessionId{5};
    s.started_at_ms = 0;
    s.status = ir::SessionStatus::Active;
    std::string row = ir::format_session_row(s);
    CHECK(row.find("Session 5") != std::string::npos);
    CHECK(row.find("Active") != std::string::npos);
}
