#include "core/version.hpp"

namespace ir {

std::string version_string() {
    return std::to_string(kVersionMajor) + '.' + std::to_string(kVersionMinor) +
           '.' + std::to_string(kVersionPatch);
}

}  // namespace ir
