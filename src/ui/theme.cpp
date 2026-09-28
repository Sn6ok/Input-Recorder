#include "ui/theme.hpp"

namespace ir {

ThemeColors theme_colors(bool dark) {
    if (dark) {
        return ThemeColors{
            /*window_bg*/ 0x202020,
            /*control_bg*/ 0x2B2B2B,
            /*text*/ 0xF0F0F0,
            /*secondary_text*/ 0xB0B0B0,
            /*border*/ 0x3C3C3C,
            /*accent_recording*/ 0xE04545,
            /*accent_paused*/ 0x969696,
        };
    }
    return ThemeColors{
        /*window_bg*/ 0xFFFFFF,
        /*control_bg*/ 0xFFFFFF,
        /*text*/ 0x141414,
        /*secondary_text*/ 0x606060,
        /*border*/ 0xC8C8C8,
        /*accent_recording*/ 0xDC3C3C,
        /*accent_paused*/ 0x808080,
    };
}

}  // namespace ir
