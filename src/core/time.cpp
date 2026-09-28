#include "core/time.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace ir {

std::int64_t Clock::now_wall_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::int64_t Clock::now_monotonic_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

Timestamp Clock::now() {
    return Timestamp{now_wall_ms(), now_monotonic_ns()};
}

std::string format_utc(std::int64_t wall_ms) {
    const std::time_t secs = static_cast<std::time_t>(wall_ms / 1000);
    const int millis = static_cast<int>(((wall_ms % 1000) + 1000) % 1000);

    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &secs);
#else
    gmtime_r(&secs, &tm);
#endif

    // 24 bytes suffice for "YYYY-MM-DD HH:MM:SS.mmm"; sized generously so a
    // worst-case (out-of-range tm) formatting can never truncate.
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                  tm.tm_min, tm.tm_sec, millis);
    return std::string(buf);
}

}  // namespace ir
