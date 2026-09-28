#pragma once

// A crash-safe, append-only overflow log for events (spec §469-§476). When the
// primary storage path is unavailable (disk full, database locked, open
// failure) the coordinator switches to appending events here so recent input is
// not lost; on recovery the buffer is replayed into the database and cleared.
//
// Format: a sequence of [u32 little-endian length][serialized event] records,
// flushed after every append. drain() tolerates a truncated final record (a
// crash mid-write), stopping cleanly at the first incomplete one. OS-free and
// unit-tested with temp files.

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "core/event.hpp"

namespace ir {

class EmergencyBuffer {
public:
    static constexpr std::uint64_t kDefaultMaxBytes = 64ull * 1024 * 1024;

    EmergencyBuffer() = default;
    ~EmergencyBuffer() { close(); }

    EmergencyBuffer(const EmergencyBuffer&) = delete;
    EmergencyBuffer& operator=(const EmergencyBuffer&) = delete;

    // Opens (creating/appending) the buffer file. Existing content is preserved
    // so a prior run's overflow can still be recovered.
    bool open(const std::string& path, std::uint64_t max_bytes = kDefaultMaxBytes);
    void close();
    bool is_open() const { return out_.is_open(); }

    // Appends one event (durably flushed). Returns false if closed or if the
    // size cap would be exceeded (sets overflowed()).
    bool append(const Event& e);

    // Reads back every intact record. Stops at a truncated tail rather than
    // failing, so a crash mid-append loses at most the last partial event.
    std::vector<Event> drain() const;

    // Empties the buffer (after a successful replay into storage).
    bool clear();

    std::uint64_t bytes_written() const { return bytes_written_; }
    bool overflowed() const { return overflowed_; }
    const std::string& path() const { return path_; }

private:
    std::string path_;
    mutable std::ofstream out_;
    std::uint64_t max_bytes_ = kDefaultMaxBytes;
    std::uint64_t bytes_written_ = 0;
    bool overflowed_ = false;
};

}  // namespace ir
