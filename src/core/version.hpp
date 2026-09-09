#pragma once

#include <string>
#include <string_view>

namespace ir {

// The product name lives in exactly one place so it can be changed easily
// (spec §2: "Назва application повинна бути легко змінюваною в одному місці").
inline constexpr std::string_view kAppName = "Input Recorder";

inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;

// Returns "MAJOR.MINOR.PATCH".
std::string version_string();

}  // namespace ir
