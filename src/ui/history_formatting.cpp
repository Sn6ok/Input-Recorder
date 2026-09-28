#include "ui/history_formatting.hpp"

#include <variant>

#include "core/time.hpp"
#include "utils/unicode.hpp"

namespace ir {
namespace {

// Collapses control characters to spaces so a value never breaks a list row.
std::string sanitize_line(const std::string& s) {
    std::u32string cps = utf8_to_utf32(s);
    for (char32_t& c : cps) {
        if (c == U'\n' || c == U'\r' || c == U'\t') c = U' ';
    }
    return utf32_to_utf8(cps);
}

std::string event_detail(const Event& e) {
    switch (e.type) {
        case EventType::TextInput:
            if (const auto* t = std::get_if<TextInputData>(&e.payload))
                return "typed: " + truncate_for_display(t->text);
            return "typed";
        case EventType::Paste:
            if (const auto* t = std::get_if<TextInputData>(&e.payload))
                return "pasted: " + truncate_for_display(t->text);
            return "pasted";
        case EventType::KeyboardShortcut:
            if (const auto* s = std::get_if<ShortcutData>(&e.payload))
                return "shortcut: " + truncate_for_display(s->text);
            return "shortcut";
        case EventType::KeyDown:
            return "key down";
        case EventType::KeyUp:
            return "key up";
        case EventType::MouseButton:
            if (const auto* m = std::get_if<MouseData>(&e.payload)) {
                std::string b(to_string(m->button));
                return b + (m->pressed ? " button down" : " button up");
            }
            return "mouse button";
        case EventType::MouseWheel:
            return "mouse wheel";
        case EventType::MouseMove:
            return "mouse move";
        case EventType::ClipboardChanged:
            return "clipboard changed";
        case EventType::ContextChanged:
            return "window changed";
        case EventType::SessionStarted:
            return "recording started";
        case EventType::SessionEnded:
            return "recording stopped";
        case EventType::Diagnostic:
            return "diagnostic";
        case EventType::Unknown:
            break;
    }
    return std::string(to_string(e.type));
}

}  // namespace

std::string truncate_for_display(std::string_view utf8,
                                 std::size_t max_code_points) {
    std::u32string cps = utf8_to_utf32(utf8);
    for (char32_t& c : cps) {
        if (c == U'\n' || c == U'\r' || c == U'\t') c = U' ';
    }
    if (cps.size() <= max_code_points) {
        return utf32_to_utf8(cps);
    }
    cps.resize(max_code_points);
    return utf32_to_utf8(cps) + "\xE2\x80\xA6";  // U+2026 HORIZONTAL ELLIPSIS
}

std::string format_timestamp(std::int64_t wall_ms) {
    std::string full = format_utc(wall_ms);  // "YYYY-MM-DD HH:MM:SS.mmm"
    return full.size() >= 19 ? full.substr(0, 19) : full;
}

std::string format_event_row(const Event& e) {
    return format_timestamp(e.time.wall_ms) + "  " + sanitize_line(event_detail(e));
}

std::string format_clipboard_row(const ClipboardEntry& e) {
    std::string row = format_timestamp(e.timestamp_ms) + "  clip: " +
                      truncate_for_display(e.text);
    if (e.truncated) row += " (truncated)";
    return row;
}

std::string format_session_row(const Session& s) {
    std::string row = "Session " + std::to_string(s.id.value) + "  started " +
                      format_timestamp(s.started_at_ms);
    row += "  (";
    row += to_string(s.status);
    row += ")";
    return row;
}

}  // namespace ir
