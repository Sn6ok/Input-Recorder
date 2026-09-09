#include "framework/test_framework.hpp"

#include "core/event.hpp"
#include "core/serialization.hpp"

namespace {

ir::Event make_base() {
    ir::Event e;
    e.id = ir::EventId{100};
    e.session = ir::SessionId{5};
    e.context = ir::ContextId{9};
    e.time = ir::Timestamp{1788944401123LL, 123456789LL};
    e.category = ir::EventCategory::UserInput;
    e.flags = ir::EventFlags::None;
    return e;
}

// Round-trips a full event through serialize/deserialize and checks equality.
void round_trip(const ir::Event& e) {
    auto bytes = ir::serialize_event(e);
    ir::Event out;
    REQUIRE(ir::deserialize_event(bytes, out));
    CHECK(out == e);
}

}  // namespace

TEST_CASE("core.serialization", "key event round-trips") {
    ir::Event e = make_base();
    e.type = ir::EventType::KeyDown;
    e.flags = ir::EventFlags::Extended | ir::EventFlags::ModCtrl;
    e.payload = ir::KeyEventData{0x41, 0x1E};
    round_trip(e);
}

TEST_CASE("core.serialization", "text input round-trips (Unicode)") {
    ir::Event e = make_base();
    e.type = ir::EventType::TextInput;
    // "Hello Привіт 😀" encoded as explicit UTF-8 bytes (layout-independent).
    e.payload = ir::TextInputData{
        "Hello \xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD1\x96\xD1\x82 \xF0\x9F\x98\x80"};
    round_trip(e);
}

TEST_CASE("core.serialization", "shortcut round-trips") {
    ir::Event e = make_base();
    e.type = ir::EventType::KeyboardShortcut;
    e.category = ir::EventCategory::UserInput;
    e.payload = ir::ShortcutData{0x18u, 0x43, "Ctrl+C"};
    round_trip(e);
}

TEST_CASE("core.serialization", "mouse round-trips with negative coords") {
    ir::Event e = make_base();
    e.type = ir::EventType::MouseButton;
    e.category = ir::EventCategory::UserInput;
    e.payload = ir::MouseData{ir::MouseButton::Right, true, -120, -3000, 0};
    round_trip(e);
}

TEST_CASE("core.serialization", "clipboard ref round-trips") {
    ir::Event e = make_base();
    e.type = ir::EventType::Paste;
    e.category = ir::EventCategory::ClipboardEvent;
    e.payload = ir::ClipboardRef{ir::ClipboardEntryId{9999}};
    round_trip(e);
}

TEST_CASE("core.serialization", "monostate (session) round-trips") {
    ir::Event e = make_base();
    e.type = ir::EventType::SessionStarted;
    e.category = ir::EventCategory::SystemEvent;
    e.payload = std::monostate{};
    round_trip(e);
}

TEST_CASE("core.serialization", "diagnostic round-trips") {
    ir::Event e = make_base();
    e.type = ir::EventType::Diagnostic;
    e.category = ir::EventCategory::SystemEvent;
    e.payload = ir::DiagnosticData{"data-loss gap: storage unavailable"};
    round_trip(e);
}

TEST_CASE("core.serialization", "payload-only round-trip") {
    ir::EventPayload in = ir::TextInputData{"hello"};
    auto bytes = ir::serialize_payload(in);
    ir::EventPayload out;
    REQUIRE(ir::deserialize_payload(bytes, out));
    CHECK(in == out);
}

TEST_CASE("core.serialization", "truncated blob fails cleanly") {
    ir::Event e = make_base();
    e.type = ir::EventType::TextInput;
    e.payload = ir::TextInputData{"some text"};
    auto bytes = ir::serialize_event(e);
    bytes.resize(bytes.size() / 2);  // chop it
    ir::Event out;
    CHECK(!ir::deserialize_event(bytes, out));
}

TEST_CASE("core.serialization", "bad version rejected") {
    ir::Event e = make_base();
    e.type = ir::EventType::KeyDown;
    e.payload = ir::KeyEventData{0x41, 0x1E};
    auto bytes = ir::serialize_event(e);
    bytes[0] = static_cast<std::byte>(0xFE);  // wrong version
    ir::Event out;
    CHECK(!ir::deserialize_event(bytes, out));
}
