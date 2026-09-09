#pragma once

// Small, dependency-free binary (de)serialization helpers.
//
// Integers are written little-endian via explicit shifts (portable, no UB, no
// warnings). Strings are length-prefixed (u32 byte count + raw bytes) and hold
// UTF-8. The reader is bounds-checked: on underflow it sets an error flag and
// returns zero/empty rather than reading out of range, so callers validate with
// ok() after decoding.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ir {

class ByteWriter {
public:
    void put_u8(std::uint8_t v) { buf_.push_back(static_cast<std::byte>(v)); }

    void put_u16(std::uint16_t v) {
        put_u8(static_cast<std::uint8_t>(v & 0xFF));
        put_u8(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    }

    void put_u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            put_u8(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    void put_u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            put_u8(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    void put_i64(std::int64_t v) { put_u64(static_cast<std::uint64_t>(v)); }

    void put_bool(bool v) { put_u8(v ? 1u : 0u); }

    void put_string(std::string_view s) {
        put_u32(static_cast<std::uint32_t>(s.size()));
        const auto* bytes = reinterpret_cast<const std::byte*>(s.data());
        buf_.insert(buf_.end(), bytes, bytes + s.size());
    }

    const std::vector<std::byte>& data() const { return buf_; }
    std::vector<std::byte> take() { return std::move(buf_); }
    std::size_t size() const { return buf_.size(); }

private:
    std::vector<std::byte> buf_;
};

class ByteReader {
public:
    ByteReader(const std::byte* data, std::size_t size) : p_(data), end_(data + size) {}
    explicit ByteReader(const std::vector<std::byte>& v)
        : ByteReader(v.data(), v.size()) {}

    std::uint8_t get_u8() {
        if (p_ >= end_) {
            fail();
            return 0;
        }
        return static_cast<std::uint8_t>(*p_++);
    }

    std::uint16_t get_u16() {
        std::uint16_t lo = get_u8();
        std::uint16_t hi = get_u8();
        return static_cast<std::uint16_t>(lo | (hi << 8));
    }

    std::uint32_t get_u32() {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(get_u8()) << (8 * i);
        }
        return v;
    }

    std::uint64_t get_u64() {
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<std::uint64_t>(get_u8()) << (8 * i);
        }
        return v;
    }

    std::int64_t get_i64() { return static_cast<std::int64_t>(get_u64()); }

    bool get_bool() { return get_u8() != 0; }

    std::string get_string() {
        std::uint32_t len = get_u32();
        if (!ok_ || static_cast<std::size_t>(end_ - p_) < len) {
            fail();
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p_), len);
        p_ += len;
        return s;
    }

    bool ok() const { return ok_; }
    std::size_t remaining() const { return static_cast<std::size_t>(end_ - p_); }

private:
    void fail() { ok_ = false; }

    const std::byte* p_;
    const std::byte* end_;
    bool ok_ = true;
};

}  // namespace ir
