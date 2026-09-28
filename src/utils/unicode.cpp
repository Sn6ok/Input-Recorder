#include "utils/unicode.hpp"

namespace ir {
namespace {

constexpr char32_t kReplacement = 0xFFFD;

// Appends the UTF-8 encoding of one code point to `out`.
void append_utf8(std::string& out, char32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        cp = kReplacement;
    }
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

}  // namespace

std::u32string utf8_to_utf32(std::string_view utf8) {
    std::u32string out;
    out.reserve(utf8.size());
    std::size_t i = 0;
    const std::size_t n = utf8.size();
    while (i < n) {
        const auto b0 = static_cast<unsigned char>(utf8[i]);
        char32_t cp = 0;
        std::size_t extra = 0;
        if (b0 < 0x80) {
            cp = b0;
        } else if ((b0 & 0xE0) == 0xC0) {
            cp = b0 & 0x1F;
            extra = 1;
        } else if ((b0 & 0xF0) == 0xE0) {
            cp = b0 & 0x0F;
            extra = 2;
        } else if ((b0 & 0xF8) == 0xF0) {
            cp = b0 & 0x07;
            extra = 3;
        } else {
            out.push_back(kReplacement);  // invalid leading byte
            ++i;
            continue;
        }

        if (i + extra >= n) {
            out.push_back(kReplacement);  // truncated sequence
            break;
        }

        bool valid = true;
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto bk = static_cast<unsigned char>(utf8[i + k]);
            if ((bk & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (bk & 0x3F);
        }

        if (!valid) {
            out.push_back(kReplacement);
            ++i;  // resync on the offending byte
            continue;
        }

        // Reject overlong encodings, surrogates and out-of-range values.
        const bool overlong = (extra == 1 && cp < 0x80) ||
                              (extra == 2 && cp < 0x800) ||
                              (extra == 3 && cp < 0x10000);
        if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            cp = kReplacement;
        }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

std::string utf32_to_utf8(std::u32string_view utf32) {
    std::string out;
    out.reserve(utf32.size() * 2);
    for (char32_t cp : utf32) {
        append_utf8(out, cp);
    }
    return out;
}

std::size_t utf8_codepoint_count(std::string_view utf8) {
    std::size_t count = 0;
    for (char c : utf8) {
        // Count every byte that is not a UTF-8 continuation byte (10xxxxxx).
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) {
            ++count;
        }
    }
    return count;
}

std::string utf16_to_utf8(std::u16string_view utf16) {
    std::string out;
    out.reserve(utf16.size() * 2);
    const std::size_t n = utf16.size();
    for (std::size_t i = 0; i < n; ++i) {
        char32_t cp = utf16[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n) {
            const char32_t lo = utf16[i + 1];
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                ++i;
            } else {
                cp = kReplacement;  // unpaired high surrogate
            }
        } else if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = kReplacement;  // lone surrogate
        }
        append_utf8(out, cp);
    }
    return out;
}

std::u16string utf8_to_utf16(std::string_view utf8) {
    const std::u32string cps = utf8_to_utf32(utf8);
    std::u16string out;
    out.reserve(cps.size());
    for (char32_t cp : cps) {
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

std::string utf8_truncate(std::string_view utf8, std::size_t max_bytes) {
    if (utf8.size() <= max_bytes) {
        return std::string(utf8);
    }
    std::size_t cut = max_bytes;
    // Walk back off any continuation byte so we never split a code point.
    while (cut > 0 && (static_cast<unsigned char>(utf8[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return std::string(utf8.substr(0, cut));
}

}  // namespace ir
