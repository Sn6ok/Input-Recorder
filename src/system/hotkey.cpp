#include "system/hotkey.hpp"

namespace ir {

std::uint32_t to_win32_modifiers(const Hotkey& hk) {
    std::uint32_t mods = kModNoRepeat;
    if (hk.ctrl) mods |= kModControl;
    if (hk.shift) mods |= kModShift;
    if (hk.alt) mods |= kModAlt;
    if (hk.win) mods |= kModWin;
    return mods;
}

bool is_registerable(const Hotkey& hk) {
    const bool has_modifier = hk.ctrl || hk.shift || hk.alt || hk.win;
    return hk.virtual_key != 0 && has_modifier;
}

}  // namespace ir
