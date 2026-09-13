#pragma once

// The reconstructed-text state: a code-point buffer plus cursor/selection and a
// confidence level. Copyable so it can serve as a point-in-time snapshot
// (spec §90, §211).

#include <cstddef>
#include <string>

#include "core/confidence.hpp"
#include "utils/unicode.hpp"

namespace ir {

struct TextState {
    std::u32string buffer;             // text as Unicode code points
    std::size_t cursor = 0;            // caret index into buffer (if known)
    bool cursor_known = true;          // false once the caret can't be tracked
    bool selection_active = false;     // a selection spans [anchor, cursor]
    std::size_t selection_anchor = 0;
    Confidence confidence = Confidence::High;

    std::string text_utf8() const { return utf32_to_utf8(buffer); }
    bool empty() const { return buffer.empty(); }
};

}  // namespace ir
