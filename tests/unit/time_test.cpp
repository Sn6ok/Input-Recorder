#include "framework/test_framework.hpp"

#include "core/time.hpp"

TEST_CASE("core.time", "format_utc renders a known instant") {
    // 2026-09-09 09:00:01.123 UTC == 1788944401123 ms since epoch.
    const std::int64_t ms = 1788944401123LL;
    CHECK_EQ(ir::format_utc(ms), std::string("2026-09-09 09:00:01.123"));
}

TEST_CASE("core.time", "format_utc epoch") {
    CHECK_EQ(ir::format_utc(0), std::string("1970-01-01 00:00:00.000"));
}

TEST_CASE("core.time", "monotonic clock is non-decreasing") {
    const std::int64_t a = ir::Clock::now_monotonic_ns();
    const std::int64_t b = ir::Clock::now_monotonic_ns();
    CHECK(b >= a);
}

TEST_CASE("core.time", "wall clock is positive in the present") {
    CHECK(ir::Clock::now_wall_ms() > 1700000000000LL);  // after 2023
}
