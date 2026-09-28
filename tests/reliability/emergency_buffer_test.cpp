#include "framework/test_framework.hpp"

#include <filesystem>

#include "reliability/emergency_buffer.hpp"
#include "storage/temp_db.hpp"

namespace {

ir::Event ev(std::uint64_t id, std::string text) {
    ir::Event e;
    e.id = ir::EventId{id};
    e.session = ir::SessionId{1};
    e.time = ir::Timestamp{static_cast<std::int64_t>(id), static_cast<std::int64_t>(id)};
    e.type = ir::EventType::TextInput;
    e.payload = ir::TextInputData{std::move(text)};
    return e;
}

}  // namespace

TEST_CASE("reliability.buffer", "append then drain round-trips events in order") {
    irtest::TempDbFile tmp;
    ir::EmergencyBuffer buf;
    REQUIRE(buf.open(tmp.path));
    CHECK(buf.append(ev(1, "one")));
    CHECK(buf.append(ev(2, "two")));
    CHECK(buf.append(ev(3, "three")));

    std::vector<ir::Event> got = buf.drain();
    REQUIRE(got.size() == 3u);
    CHECK(got[0] == ev(1, "one"));
    CHECK(got[2] == ev(3, "three"));
}

TEST_CASE("reliability.buffer", "content persists across reopen") {
    irtest::TempDbFile tmp;
    {
        ir::EmergencyBuffer buf;
        REQUIRE(buf.open(tmp.path));
        buf.append(ev(1, "keep"));
    }
    {
        ir::EmergencyBuffer buf;
        REQUIRE(buf.open(tmp.path));
        buf.append(ev(2, "again"));
        CHECK_EQ(buf.drain().size(), 2u);
    }
}

TEST_CASE("reliability.buffer", "clear empties the buffer") {
    irtest::TempDbFile tmp;
    ir::EmergencyBuffer buf;
    REQUIRE(buf.open(tmp.path));
    buf.append(ev(1, "x"));
    CHECK(buf.clear());
    CHECK_EQ(buf.drain().size(), 0u);
    // still usable after clear
    CHECK(buf.append(ev(2, "y")));
    CHECK_EQ(buf.drain().size(), 1u);
}

TEST_CASE("reliability.buffer", "a truncated tail is tolerated") {
    irtest::TempDbFile tmp;
    {
        ir::EmergencyBuffer buf;
        REQUIRE(buf.open(tmp.path));
        buf.append(ev(1, "intact"));
        buf.append(ev(2, "partial-will-be-cut"));
    }
    // Simulate a crash mid-append: chop the last 3 bytes of the file.
    const auto size = std::filesystem::file_size(tmp.path);
    std::filesystem::resize_file(tmp.path, size - 3);

    ir::EmergencyBuffer buf;
    REQUIRE(buf.open(tmp.path));
    std::vector<ir::Event> got = buf.drain();
    REQUIRE(got.size() == 1u);  // only the intact record survives
    CHECK(got[0] == ev(1, "intact"));
}

TEST_CASE("reliability.buffer", "size cap sets overflow and rejects the append") {
    irtest::TempDbFile tmp;
    ir::EmergencyBuffer buf;
    REQUIRE(buf.open(tmp.path, /*max_bytes=*/32));
    // First small event fits; subsequent ones exceed the tiny cap.
    buf.append(ev(1, "a"));
    bool rejected = false;
    for (int i = 0; i < 10 && !rejected; ++i) {
        if (!buf.append(ev(static_cast<std::uint64_t>(i + 2),
                           std::string(50, 'x')))) {
            rejected = true;
        }
    }
    CHECK(rejected);
    CHECK(buf.overflowed());
}
