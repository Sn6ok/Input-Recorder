#include "framework/test_framework.hpp"

#include "utils/unicode.hpp"

namespace {
// "Привіт" (6 Cyrillic code points) and a 4-byte emoji as explicit UTF-8.
const std::string kUk = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD1\x96\xD1\x82";
const std::string kEmoji = "\xF0\x9F\x98\x80";  // U+1F600
}  // namespace

TEST_CASE("utils.unicode", "ASCII round-trips") {
    auto cps = ir::utf8_to_utf32("Hello");
    CHECK_EQ(cps.size(), static_cast<std::size_t>(5));
    CHECK_EQ(ir::utf32_to_utf8(cps), std::string("Hello"));
}

TEST_CASE("utils.unicode", "Ukrainian round-trips (6 code points)") {
    auto cps = ir::utf8_to_utf32(kUk);
    CHECK_EQ(cps.size(), static_cast<std::size_t>(6));
    CHECK_EQ(ir::utf32_to_utf8(cps), kUk);
    CHECK_EQ(ir::utf8_codepoint_count(kUk), static_cast<std::size_t>(6));
}

TEST_CASE("utils.unicode", "4-byte emoji is one code point") {
    auto cps = ir::utf8_to_utf32(kEmoji);
    CHECK_EQ(cps.size(), static_cast<std::size_t>(1));
    CHECK_EQ(cps[0], static_cast<char32_t>(0x1F600));
    CHECK_EQ(ir::utf32_to_utf8(cps), kEmoji);
}

TEST_CASE("utils.unicode", "mixed scripts round-trip") {
    const std::string mixed = "a" + kEmoji + "b" + kUk;
    auto cps = ir::utf8_to_utf32(mixed);
    CHECK_EQ(cps.size(), static_cast<std::size_t>(1 + 1 + 1 + 6));
    CHECK_EQ(ir::utf32_to_utf8(cps), mixed);
}

TEST_CASE("utils.unicode", "invalid byte becomes U+FFFD") {
    std::string bad = "a\xFF" "b";
    auto cps = ir::utf8_to_utf32(bad);
    CHECK_EQ(cps.size(), static_cast<std::size_t>(3));
    CHECK_EQ(cps[1], static_cast<char32_t>(0xFFFD));
    // Re-encoding the replacement yields the 3-byte U+FFFD sequence.
    CHECK_EQ(ir::utf32_to_utf8(cps), std::string("a\xEF\xBF\xBD" "b"));
}

TEST_CASE("utils.unicode", "truncated multi-byte sequence is replaced") {
    std::string truncated = "a\xF0\x9F";  // start of a 4-byte seq, cut short
    auto cps = ir::utf8_to_utf32(truncated);
    CHECK_EQ(cps.size(), static_cast<std::size_t>(2));
    CHECK_EQ(cps[1], static_cast<char32_t>(0xFFFD));
}
