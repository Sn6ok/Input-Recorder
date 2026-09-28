#pragma once

// Pure UTF-8 <-> UTF-32 conversion (no OS dependency) so the reconstruction
// engine can edit text by code point while storing/presenting UTF-8 (spec §67).
// Invalid input is replaced with U+FFFD rather than throwing.

#include <string>
#include <string_view>

namespace ir {

// Decodes UTF-8 into a sequence of Unicode code points.
std::u32string utf8_to_utf32(std::string_view utf8);

// Encodes Unicode code points as UTF-8.
std::string utf32_to_utf8(std::u32string_view utf32);

// Number of Unicode code points in a UTF-8 string.
std::size_t utf8_codepoint_count(std::string_view utf8);

// UTF-16 <-> UTF-8 (the Windows clipboard and Win32 wide APIs use UTF-16).
std::string utf16_to_utf8(std::u16string_view utf16);
std::u16string utf8_to_utf16(std::string_view utf8);

// Returns the longest prefix of `utf8` not exceeding `max_bytes` bytes, cut only
// on a code-point boundary (never mid-sequence). Used to enforce a clipboard
// size cap without producing invalid UTF-8 (spec §111).
std::string utf8_truncate(std::string_view utf8, std::size_t max_bytes);

}  // namespace ir
