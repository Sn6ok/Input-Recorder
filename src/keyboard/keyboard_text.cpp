#include "keyboard/keyboard_text.hpp"

#include <Windows.h>

#include <cstring>

#include "utils/unicode.hpp"

namespace ir {
namespace {

bool is_modifier(std::uint16_t vk) {
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_LWIN: case VK_RWIN:
        case VK_CAPITAL: case VK_NUMLOCK: case VK_SCROLL:
            return true;
        default:
            return false;
    }
}

void set_down(unsigned char* state, int vk, bool down) {
    state[vk] = down ? 0x80 : 0x00;
}

}  // namespace

KeyboardTextResolver::KeyboardTextResolver() { reset(); }

void KeyboardTextResolver::reset() { std::memset(key_state_, 0, sizeof(key_state_)); }

std::optional<std::string> KeyboardTextResolver::on_key(std::uint16_t vk,
                                                        std::uint16_t scan,
                                                        bool key_up) {
    // Maintain modifier down-state from the event stream.
    const bool down = !key_up;
    switch (vk) {
        case VK_LSHIFT: case VK_RSHIFT: set_down(key_state_, VK_SHIFT, down); break;
        case VK_LCONTROL: case VK_RCONTROL: set_down(key_state_, VK_CONTROL, down); break;
        case VK_LMENU: case VK_RMENU: set_down(key_state_, VK_MENU, down); break;
        default: break;
    }
    if (vk < 256) set_down(key_state_, vk, down);

    // Sync toggle keys (Caps/Num/Scroll) from the OS so ToUnicodeEx casing works.
    key_state_[VK_CAPITAL] = static_cast<unsigned char>(GetKeyState(VK_CAPITAL) & 0x01);
    key_state_[VK_NUMLOCK] = static_cast<unsigned char>(GetKeyState(VK_NUMLOCK) & 0x01);

    if (key_up || is_modifier(vk)) return std::nullopt;

    const bool ctrl = (key_state_[VK_CONTROL] & 0x80) != 0;
    const bool alt = (key_state_[VK_MENU] & 0x80) != 0;
    // Ctrl+key / Alt+key are shortcuts, not text. AltGr (Ctrl+Alt) may produce
    // characters on some layouts, so allow that combination.
    if ((ctrl && !alt) || (alt && !ctrl)) return std::nullopt;

    // Resolve characters with the FOREGROUND window's keyboard layout, not the
    // capture thread's own layout. The hook runs on the app's capture thread
    // (US English by default); using its layout mis-resolves input typed in
    // another layout (e.g. Ukrainian) into the Latin letters of the physical
    // keys instead of the real characters. GetKeyboardLayout(threadId) of the
    // foreground thread gives the layout the user is actually typing with.
    HKL layout = GetKeyboardLayout(0);
    if (HWND fg = GetForegroundWindow()) {
        const DWORD tid = GetWindowThreadProcessId(fg, nullptr);
        if (tid != 0) layout = GetKeyboardLayout(tid);
    }
    wchar_t buffer[8] = {};
    const int rc = ToUnicodeEx(vk, scan, key_state_, buffer,
                               static_cast<int>(std::size(buffer)), 0, layout);
    if (rc == -1) {
        // Dead key: flush it so it does not corrupt the next translation, and
        // emit no text now (the composed character arrives with the next key).
        ToUnicodeEx(vk, scan, key_state_, buffer,
                    static_cast<int>(std::size(buffer)), 0, layout);
        return std::nullopt;
    }
    if (rc <= 0) return std::nullopt;

    std::u16string u16(reinterpret_cast<const char16_t*>(buffer),
                       static_cast<std::size_t>(rc));
    std::string utf8 = utf16_to_utf8(u16);

    // Drop control characters (Enter/Tab/Backspace/Esc) — handled as edit keys.
    if (utf8.size() == 1 && static_cast<unsigned char>(utf8[0]) < 0x20) {
        return std::nullopt;
    }
    if (utf8.empty()) return std::nullopt;
    return utf8;
}

}  // namespace ir
