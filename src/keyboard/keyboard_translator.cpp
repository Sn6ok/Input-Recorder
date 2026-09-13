#include "keyboard/keyboard_translator.hpp"

namespace ir {
namespace {

// Windows virtual-key codes we need for modifier tracking. Defined locally so
// the translator stays free of <Windows.h> and testable everywhere.
constexpr std::uint16_t kVkShift = 0x10;
constexpr std::uint16_t kVkControl = 0x11;
constexpr std::uint16_t kVkMenu = 0x12;  // Alt
constexpr std::uint16_t kVkLShift = 0xA0;
constexpr std::uint16_t kVkRShift = 0xA1;
constexpr std::uint16_t kVkLControl = 0xA2;
constexpr std::uint16_t kVkRControl = 0xA3;
constexpr std::uint16_t kVkLMenu = 0xA4;
constexpr std::uint16_t kVkRMenu = 0xA5;
constexpr std::uint16_t kVkLWin = 0x5B;
constexpr std::uint16_t kVkRWin = 0x5C;

}  // namespace

EventFlags KeyboardTranslator::modifier_flags() const {
    EventFlags f = EventFlags::None;
    if (down_[kVkControl] || down_[kVkLControl] || down_[kVkRControl])
        f |= EventFlags::ModCtrl;
    if (down_[kVkShift] || down_[kVkLShift] || down_[kVkRShift])
        f |= EventFlags::ModShift;
    if (down_[kVkMenu] || down_[kVkLMenu] || down_[kVkRMenu])
        f |= EventFlags::ModAlt;
    if (down_[kVkLWin] || down_[kVkRWin]) f |= EventFlags::ModWin;
    return f;
}

Event KeyboardTranslator::translate(const RawKeyboardInput& raw, Timestamp ts) {
    const std::uint16_t vk = static_cast<std::uint16_t>(raw.virtual_key & 0xFF);

    bool repeat = false;
    if (raw.key_up) {
        down_[vk] = false;
    } else {
        repeat = down_[vk];  // already down => auto-repeat (spec §81)
        down_[vk] = true;
    }

    Event e;
    e.type = raw.key_up ? EventType::KeyUp : EventType::KeyDown;
    e.category = EventCategory::UserInput;
    e.time = ts;

    EventFlags flags = EventFlags::None;
    if (raw.extended) flags |= EventFlags::Extended;
    if (raw.injected) flags |= EventFlags::Injected;
    if (repeat) flags |= EventFlags::KeyRepeat;
    flags |= modifier_flags();
    e.flags = flags;

    e.payload = KeyEventData{static_cast<std::uint16_t>(raw.virtual_key),
                             static_cast<std::uint16_t>(raw.scan_code)};
    return e;
}

}  // namespace ir
