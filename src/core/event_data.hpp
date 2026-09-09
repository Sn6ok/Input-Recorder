#pragma once

// Per-event-type payloads. An event only carries the data its type needs
// (spec §59: no giant universal object). These are combined in a std::variant
// as EventPayload in event.hpp.

#include <cstdint>
#include <string>
#include <string_view>

#include "core/ids.hpp"

namespace ir {

// Raw keyboard data preserved for reconstruction/debugging (spec §61).
// The injected/extended/repeat/modifier bits live in the event's flags.
struct KeyEventData {
    std::uint16_t virtual_key = 0;
    std::uint16_t scan_code = 0;

    friend bool operator==(const KeyEventData& a, const KeyEventData& b) {
        return a.virtual_key == b.virtual_key && a.scan_code == b.scan_code;
    }
};

// Resolved characters produced by input (spec §68). UTF-8.
struct TextInputData {
    std::string text;

    friend bool operator==(const TextInputData& a, const TextInputData& b) {
        return a.text == b.text;
    }
};

// A detected keyboard combination (spec §64). `modifier_flags` reuses the
// EventFlags Mod* bits; `text` is a canonical label such as "Ctrl+C".
struct ShortcutData {
    std::uint32_t modifier_flags = 0;
    std::uint16_t key_virtual_key = 0;
    std::string text;

    friend bool operator==(const ShortcutData& a, const ShortcutData& b) {
        return a.modifier_flags == b.modifier_flags &&
               a.key_virtual_key == b.key_virtual_key && a.text == b.text;
    }
};

enum class MouseButton : std::uint8_t {
    None = 0,
    Left = 1,
    Right = 2,
    Middle = 3,
    X1 = 4,
    X2 = 5,
};

constexpr std::string_view to_string(MouseButton b) {
    switch (b) {
        case MouseButton::None: return "None";
        case MouseButton::Left: return "Left";
        case MouseButton::Right: return "Right";
        case MouseButton::Middle: return "Middle";
        case MouseButton::X1: return "X1";
        case MouseButton::X2: return "X2";
    }
    return "None";
}

// Mouse button / wheel / move data (spec §115, §118, §119).
struct MouseData {
    MouseButton button = MouseButton::None;
    bool pressed = false;         // true = down, false = up (button events)
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t wheel_delta = 0; // WHEEL_DELTA units (wheel events)

    friend bool operator==(const MouseData& a, const MouseData& b) {
        return a.button == b.button && a.pressed == b.pressed && a.x == b.x &&
               a.y == b.y && a.wheel_delta == b.wheel_delta;
    }
};

// Reference to a clipboard entry stored separately (spec §109). Used by
// ClipboardChanged and Paste events so large text is not duplicated per event.
struct ClipboardRef {
    ClipboardEntryId entry;

    friend bool operator==(const ClipboardRef& a, const ClipboardRef& b) {
        return a.entry == b.entry;
    }
};

// Internal diagnostic marker (e.g. a recorded data-loss gap, spec §471). Never
// contains reconstructed text or clipboard contents (spec §322, §329).
struct DiagnosticData {
    std::string message;

    friend bool operator==(const DiagnosticData& a, const DiagnosticData& b) {
        return a.message == b.message;
    }
};

}  // namespace ir
