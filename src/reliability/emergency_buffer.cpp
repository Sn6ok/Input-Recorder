#include "reliability/emergency_buffer.hpp"

#include <cstdint>

#include "core/serialization.hpp"

namespace ir {
namespace {

void put_u32_le(std::ofstream& os, std::uint32_t v) {
    char b[4];
    b[0] = static_cast<char>(v & 0xFF);
    b[1] = static_cast<char>((v >> 8) & 0xFF);
    b[2] = static_cast<char>((v >> 16) & 0xFF);
    b[3] = static_cast<char>((v >> 24) & 0xFF);
    os.write(b, 4);
}

bool get_u32_le(std::ifstream& is, std::uint32_t& out) {
    char b[4];
    if (!is.read(b, 4)) return false;
    out = static_cast<std::uint32_t>(static_cast<unsigned char>(b[0])) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(b[1])) << 8) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(b[2])) << 16) |
          (static_cast<std::uint32_t>(static_cast<unsigned char>(b[3])) << 24);
    return true;
}

std::uint64_t file_size(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return 0;
    const std::streampos end = f.tellg();
    return end > 0 ? static_cast<std::uint64_t>(end) : 0;
}

}  // namespace

bool EmergencyBuffer::open(const std::string& path, std::uint64_t max_bytes) {
    close();
    path_ = path;
    max_bytes_ = max_bytes;
    bytes_written_ = file_size(path);  // preserve prior content
    overflowed_ = false;
    out_.open(path, std::ios::binary | std::ios::app);
    return out_.is_open();
}

void EmergencyBuffer::close() {
    if (out_.is_open()) {
        out_.flush();
        out_.close();
    }
}

bool EmergencyBuffer::append(const Event& e) {
    if (!out_.is_open()) return false;
    std::vector<std::byte> bytes = serialize_event(e);
    const std::uint64_t record = 4 + bytes.size();
    if (bytes_written_ + record > max_bytes_) {
        overflowed_ = true;
        return false;
    }
    put_u32_le(out_, static_cast<std::uint32_t>(bytes.size()));
    out_.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    out_.flush();  // durability: survive a crash right after this returns
    if (!out_) return false;
    bytes_written_ += record;
    return true;
}

std::vector<Event> EmergencyBuffer::drain() const {
    std::vector<Event> events;
    std::ifstream in(path_, std::ios::binary);
    if (!in) return events;

    for (;;) {
        std::uint32_t len = 0;
        if (!get_u32_le(in, len)) break;                 // no more records
        if (len == 0 || len > 64u * 1024 * 1024) break;  // corrupt/absurd
        std::vector<std::byte> bytes(len);
        if (!in.read(reinterpret_cast<char*>(bytes.data()),
                     static_cast<std::streamsize>(len))) {
            break;  // truncated tail (crash mid-write) — stop cleanly
        }
        Event e;
        if (deserialize_event(bytes, e)) events.push_back(std::move(e));
    }
    return events;
}

bool EmergencyBuffer::clear() {
    const bool was_open = out_.is_open();
    if (was_open) out_.close();
    {
        std::ofstream trunc(path_, std::ios::binary | std::ios::trunc);
        if (!trunc) return false;
    }
    bytes_written_ = 0;
    overflowed_ = false;
    if (was_open) {
        out_.open(path_, std::ios::binary | std::ios::app);
        return out_.is_open();
    }
    return true;
}

}  // namespace ir
