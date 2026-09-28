#pragma once

// Test helper: a unique temporary database file path that is removed (together
// with its WAL/SHM sidecars) when the RAII object goes out of scope. Uses
// std::filesystem so it works under both the MSVC and POSIX test builds.

#include <atomic>
#include <filesystem>
#include <random>
#include <string>

namespace irtest {

inline std::string make_temp_db_path() {
    static std::atomic<unsigned long long> counter{0};
    std::random_device rd;
    const unsigned long long n =
        (static_cast<unsigned long long>(rd()) << 21) ^ counter.fetch_add(1);
    const auto p = std::filesystem::temp_directory_path() /
                   ("ir_test_" + std::to_string(n) + ".db");
    return p.string();
}

struct TempDbFile {
    std::string path = make_temp_db_path();

    TempDbFile() = default;
    TempDbFile(const TempDbFile&) = delete;
    TempDbFile& operator=(const TempDbFile&) = delete;

    ~TempDbFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        std::filesystem::remove(path + "-wal", ec);
        std::filesystem::remove(path + "-shm", ec);
        std::filesystem::remove(path + "-journal", ec);
    }
};

}  // namespace irtest
