#pragma once

// Reconstruction confidence (spec §90). The engine must never present an
// approximate reconstruction as guaranteed-correct, so reconstructed text and
// snapshots carry a confidence level.

#include <cstdint>
#include <string_view>

namespace ir {

enum class Confidence : std::uint8_t {
    Unknown = 0,  // engine could not determine the resulting text
    Low = 1,      // e.g. cursor position was unknown
    Medium = 2,   // mostly reliable with some uncertainty
    High = 3,     // exact reconstruction
};

constexpr std::string_view to_string(Confidence c) {
    switch (c) {
        case Confidence::Unknown: return "Unknown";
        case Confidence::Low: return "Low";
        case Confidence::Medium: return "Medium";
        case Confidence::High: return "High";
    }
    return "Unknown";
}

}  // namespace ir
