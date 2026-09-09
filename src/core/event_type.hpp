#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace ir {

// Kinds of events the pipeline carries (spec §60). Values are stable on-disk
// identifiers — append new kinds at the end, never renumber.
enum class EventType : std::uint8_t {
    Unknown = 0,
    KeyDown = 1,
    KeyUp = 2,
    TextInput = 3,          // resolved characters produced by input
    KeyboardShortcut = 4,   // e.g. Ctrl+C, detected as a combination
    MouseButton = 5,
    MouseWheel = 6,
    MouseMove = 7,
    ClipboardChanged = 8,
    Paste = 9,              // Ctrl+V / paste correlated with a clipboard entry
    ContextChanged = 10,    // foreground window/process changed
    SessionStarted = 11,
    SessionEnded = 12,
    Diagnostic = 13,        // internal marker (e.g. storage failure, data-loss gap)
};

// Where an event came from (spec §89). Keeps user text separate from the app's
// own commands and system events so they never get mixed into reconstruction.
enum class EventCategory : std::uint8_t {
    UserInput = 0,
    ApplicationCommand = 1,
    SystemEvent = 2,
    ClipboardEvent = 3,
    ContextEvent = 4,
};

// Per-event bit flags (spec §61, §82, §83, §111).
enum class EventFlags : std::uint32_t {
    None = 0,
    Injected = 1u << 0,       // software-generated input (LLKHF_INJECTED)
    Extended = 1u << 1,       // extended scan code
    KeyRepeat = 1u << 2,      // auto-repeat KeyDown
    ModCtrl = 1u << 3,        // modifier state captured at event time
    ModShift = 1u << 4,
    ModAlt = 1u << 5,
    ModWin = 1u << 6,
    Truncated = 1u << 7,      // payload/clipboard content was truncated
    Uncertain = 1u << 8,      // reconstruction uncertainty marker
    SelfGenerated = 1u << 9,  // originated from Input Recorder itself
};

constexpr EventFlags operator|(EventFlags a, EventFlags b) {
    using U = std::underlying_type_t<EventFlags>;
    return static_cast<EventFlags>(static_cast<U>(a) | static_cast<U>(b));
}
constexpr EventFlags operator&(EventFlags a, EventFlags b) {
    using U = std::underlying_type_t<EventFlags>;
    return static_cast<EventFlags>(static_cast<U>(a) & static_cast<U>(b));
}
constexpr EventFlags& operator|=(EventFlags& a, EventFlags b) {
    a = a | b;
    return a;
}
constexpr bool has_flag(EventFlags value, EventFlags flag) {
    using U = std::underlying_type_t<EventFlags>;
    return (static_cast<U>(value) & static_cast<U>(flag)) != 0;
}

constexpr std::string_view to_string(EventType t) {
    switch (t) {
        case EventType::Unknown: return "Unknown";
        case EventType::KeyDown: return "KeyDown";
        case EventType::KeyUp: return "KeyUp";
        case EventType::TextInput: return "TextInput";
        case EventType::KeyboardShortcut: return "KeyboardShortcut";
        case EventType::MouseButton: return "MouseButton";
        case EventType::MouseWheel: return "MouseWheel";
        case EventType::MouseMove: return "MouseMove";
        case EventType::ClipboardChanged: return "ClipboardChanged";
        case EventType::Paste: return "Paste";
        case EventType::ContextChanged: return "ContextChanged";
        case EventType::SessionStarted: return "SessionStarted";
        case EventType::SessionEnded: return "SessionEnded";
        case EventType::Diagnostic: return "Diagnostic";
    }
    return "Unknown";
}

constexpr std::string_view to_string(EventCategory c) {
    switch (c) {
        case EventCategory::UserInput: return "UserInput";
        case EventCategory::ApplicationCommand: return "ApplicationCommand";
        case EventCategory::SystemEvent: return "SystemEvent";
        case EventCategory::ClipboardEvent: return "ClipboardEvent";
        case EventCategory::ContextEvent: return "ContextEvent";
    }
    return "UserInput";
}

}  // namespace ir
