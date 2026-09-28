#pragma once

// Resolves the actual character(s) a key press produces, honoring the active
// keyboard layout, Shift/Caps/AltGr and dead keys (spec §68). This is what turns
// key presses into TextInput events so the reconstruction engine can rebuild the
// text the user typed. Windows-only (uses ToUnicodeEx + the keyboard layout);
// the header avoids <Windows.h>.
//
// Ctrl+key and Alt+key (without AltGr) are treated as shortcuts and produce no
// text. Control characters (Enter/Tab/Backspace/Esc) produce no text either —
// they are handled as editing keys via the KeyDown path. State is confined to
// the capture thread.

#include <cstdint>
#include <optional>
#include <string>

namespace ir {

class KeyboardTextResolver {
public:
    KeyboardTextResolver();

    // Feeds a key event; returns the resolved UTF-8 text for a text-producing
    // key-down, or std::nullopt (modifier, shortcut, dead key, control char, or
    // any key-up). Must be called for every key event so modifier/lock state
    // stays correct.
    std::optional<std::string> on_key(std::uint16_t virtual_key,
                                      std::uint16_t scan_code, bool key_up);

    void reset();

private:
    // 256-byte Win32 keyboard-state array, maintained across calls.
    unsigned char key_state_[256];
};

}  // namespace ir
