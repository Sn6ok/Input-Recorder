#include "reconstruction/reconstruction_engine.hpp"

#include <algorithm>
#include <cstdio>

namespace ir {
namespace {

// Editing/navigation virtual-key codes (defined locally to keep the engine
// OS-independent).
constexpr std::uint16_t kVkBack = 0x08;
constexpr std::uint16_t kVkTab = 0x09;
constexpr std::uint16_t kVkReturn = 0x0D;
constexpr std::uint16_t kVkEnd = 0x23;
constexpr std::uint16_t kVkHome = 0x24;
constexpr std::uint16_t kVkLeft = 0x25;
constexpr std::uint16_t kVkUp = 0x26;
constexpr std::uint16_t kVkRight = 0x27;
constexpr std::uint16_t kVkDown = 0x28;
constexpr std::uint16_t kVkDelete = 0x2E;

constexpr std::uint16_t kVkA = 0x41;
constexpr std::uint16_t kVkC = 0x43;
constexpr std::uint16_t kVkX = 0x58;
constexpr std::uint16_t kVkZ = 0x5A;

bool is_word_separator(char32_t c) {
    return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r';
}

bool is_modifier_vk(std::uint16_t vk) {
    switch (vk) {
        case 0x10: case 0x11: case 0x12:        // Shift/Ctrl/Alt
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:  // L/R
        case 0x5B: case 0x5C:                    // L/R Win
        case 0x14: case 0x90: case 0x91:         // Caps/Num/Scroll lock
            return true;
        default:
            return false;
    }
}

// A short human label for a virtual key, used in annotation markers.
std::string vk_key_label(std::uint16_t vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        return std::string(1, static_cast<char>(vk));
    }
    switch (vk) {
        case 0x0D: return "Enter";
        case 0x09: return "Tab";
        case 0x1B: return "Esc";
        case 0x20: return "Space";
        case 0x08: return "Backspace";
        case 0x2E: return "Delete";
        case 0x25: return "Left";
        case 0x27: return "Right";
        case 0x26: return "Up";
        case 0x28: return "Down";
        case 0x24: return "Home";
        case 0x23: return "End";
        case 0x21: return "PageUp";
        case 0x22: return "PageDown";
        case 0x2D: return "Insert";
        default: break;
    }
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x70 + 1);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "VK_%02X", vk);
    return std::string(buf);
}

}  // namespace

void ReconstructionEngine::lower_confidence(Confidence c) {
    if (static_cast<int>(c) < static_cast<int>(state_.confidence)) {
        state_.confidence = c;
    }
}

void ReconstructionEngine::clear_selection() {
    state_.selection_active = false;
}

void ReconstructionEngine::delete_selection() {
    if (!state_.selection_active) return;
    if (!state_.cursor_known) {
        clear_selection();
        lower_confidence(Confidence::Medium);
        return;
    }
    const std::size_t lo = std::min(state_.selection_anchor, state_.cursor);
    const std::size_t hi = std::max(state_.selection_anchor, state_.cursor);
    state_.buffer.erase(lo, hi - lo);
    state_.cursor = lo;
    state_.selection_active = false;
}

void ReconstructionEngine::insert(const std::u32string& text) {
    if (state_.selection_active) {
        delete_selection();
    }
    if (state_.cursor_known) {
        state_.buffer.insert(state_.cursor, text);
        state_.cursor += text.size();
    } else {
        // Caret unknown: appending at the end is the best available guess.
        state_.buffer.append(text);
        lower_confidence(Confidence::Medium);
    }
}

void ReconstructionEngine::backspace() {
    if (state_.selection_active) {
        delete_selection();
        return;
    }
    if (state_.cursor_known) {
        if (state_.cursor > 0) {
            state_.buffer.erase(state_.cursor - 1, 1);
            --state_.cursor;
        }
    } else if (!state_.buffer.empty()) {
        state_.buffer.pop_back();
        lower_confidence(Confidence::Medium);
    }
}

void ReconstructionEngine::delete_forward() {
    if (state_.selection_active) {
        delete_selection();
        return;
    }
    if (state_.cursor_known) {
        if (state_.cursor < state_.buffer.size()) {
            state_.buffer.erase(state_.cursor, 1);
        }
    } else {
        lower_confidence(Confidence::Medium);  // unknown caret -> can't tell
    }
}

void ReconstructionEngine::begin_selection_if_needed(bool select) {
    if (select) {
        if (!state_.selection_active) {
            state_.selection_active = true;
            state_.selection_anchor = state_.cursor;
        }
    } else {
        clear_selection();
    }
}

void ReconstructionEngine::move_left(bool select) {
    if (!state_.cursor_known) return;
    begin_selection_if_needed(select);
    if (state_.cursor > 0) --state_.cursor;
}

void ReconstructionEngine::move_right(bool select) {
    if (!state_.cursor_known) return;
    begin_selection_if_needed(select);
    if (state_.cursor < state_.buffer.size()) ++state_.cursor;
}

void ReconstructionEngine::move_home(bool select) {
    if (!state_.cursor_known) return;
    begin_selection_if_needed(select);
    std::size_t start = 0;
    if (state_.cursor > 0) {
        const std::size_t nl = state_.buffer.rfind(U'\n', state_.cursor - 1);
        start = (nl == std::u32string::npos) ? 0 : nl + 1;
    }
    state_.cursor = start;
}

void ReconstructionEngine::move_end(bool select) {
    if (!state_.cursor_known) return;
    begin_selection_if_needed(select);
    const std::size_t nl = state_.buffer.find(U'\n', state_.cursor);
    state_.cursor = (nl == std::u32string::npos) ? state_.buffer.size() : nl;
}

void ReconstructionEngine::move_vertical() {
    // Column tracking across lines cannot be done reliably from key events
    // alone; mark the caret unknown rather than guess (spec §73).
    state_.cursor_known = false;
    clear_selection();
    lower_confidence(Confidence::Medium);
}

void ReconstructionEngine::word_move(bool right, bool select) {
    if (!state_.cursor_known) return;
    begin_selection_if_needed(select);
    const auto& b = state_.buffer;
    std::size_t c = state_.cursor;
    if (right) {
        while (c < b.size() && !is_word_separator(b[c])) ++c;
        while (c < b.size() && is_word_separator(b[c])) ++c;
    } else {
        while (c > 0 && is_word_separator(b[c - 1])) --c;
        while (c > 0 && !is_word_separator(b[c - 1])) --c;
    }
    state_.cursor = c;
}

void ReconstructionEngine::select_all() {
    if (state_.buffer.empty()) {
        state_.selection_active = false;
        state_.cursor = 0;
        state_.cursor_known = true;
        return;
    }
    state_.selection_active = true;
    state_.selection_anchor = 0;
    state_.cursor = state_.buffer.size();
    state_.cursor_known = true;
}

void ReconstructionEngine::handle_key_down(const Event& event) {
    const auto* key = std::get_if<KeyEventData>(&event.payload);
    if (key == nullptr) return;
    const std::uint16_t vk = key->virtual_key;
    const bool shift = has_flag(event.flags, EventFlags::ModShift);
    const bool ctrl = has_flag(event.flags, EventFlags::ModCtrl);

    switch (vk) {
        case kVkBack: backspace(); break;
        case kVkDelete: delete_forward(); break;
        case kVkReturn: insert(U"\n"); break;
        case kVkTab: insert(U"\t"); break;
        case kVkLeft:
            if (ctrl) word_move(false, shift);
            else move_left(shift);
            break;
        case kVkRight:
            if (ctrl) word_move(true, shift);
            else move_right(shift);
            break;
        case kVkHome: move_home(shift); break;
        case kVkEnd: move_end(shift); break;
        case kVkUp:
        case kVkDown: move_vertical(); break;
        default:
            // Printable keys arrive as TextInput; ignore them here.
            break;
    }
}

void ReconstructionEngine::handle_shortcut(const ShortcutData& shortcut) {
    const bool ctrl =
        (shortcut.modifier_flags & static_cast<std::uint32_t>(EventFlags::ModCtrl)) != 0;
    if (!ctrl) return;  // only Ctrl-based shortcuts affect the buffer here
    switch (shortcut.key_virtual_key) {
        case kVkA: select_all(); break;
        case kVkX:  // cut removes the selection
            if (state_.selection_active) delete_selection();
            break;
        case kVkZ:  // undo cannot be reconstructed reliably (spec §72)
            lower_confidence(Confidence::Low);
            break;
        case kVkC:  // copy: no text change
        default:    // other shortcuts: never inserted as literal text
            break;
    }
}

void ReconstructionEngine::append_marker(const std::string& label) {
    // CRLF so Win32 multi-line EDIT controls actually break the line, putting
    // each key/shortcut marker on its own line.
    if (!annotated_.empty() && annotated_.back() != '\n') annotated_ += "\r\n";
    annotated_ += '(';
    annotated_ += label;
    annotated_ += ")\r\n";
}

void ReconstructionEngine::annotated_backspace() {
    if (annotated_.empty()) return;
    const char last = annotated_.back();
    if (last == '\n' || last == ')') return;  // don't corrupt a marker
    annotated_.pop_back();
    // Remove the rest of a multi-byte UTF-8 code point.
    while (!annotated_.empty() &&
           (static_cast<unsigned char>(annotated_.back()) & 0xC0) == 0x80) {
        annotated_.pop_back();
    }
}

void ReconstructionEngine::annotate(const Event& event) {
    switch (event.type) {
        case EventType::TextInput:
        case EventType::Paste:
            if (const auto* t = std::get_if<TextInputData>(&event.payload)) {
                annotated_ += t->text;
            }
            break;
        case EventType::KeyboardShortcut:
            if (const auto* s = std::get_if<ShortcutData>(&event.payload)) {
                append_marker(s->text.empty() ? "shortcut" : s->text);
            }
            break;
        case EventType::KeyDown: {
            const auto* k = std::get_if<KeyEventData>(&event.payload);
            if (k == nullptr) break;
            const std::uint16_t vk = k->virtual_key;
            if (is_modifier_vk(vk)) break;

            const bool ctrl = has_flag(event.flags, EventFlags::ModCtrl);
            const bool alt = has_flag(event.flags, EventFlags::ModAlt);
            const bool win = has_flag(event.flags, EventFlags::ModWin);
            const bool shift = has_flag(event.flags, EventFlags::ModShift);

            if (ctrl || alt || win) {
                // A shortcut combination, e.g. "Ctrl + Shift + S".
                std::string label;
                if (ctrl) label += "Ctrl + ";
                if (shift) label += "Shift + ";
                if (alt) label += "Alt + ";
                if (win) label += "Win + ";
                label += vk_key_label(vk);
                append_marker(label);
            } else if (vk == 0x0D) {
                append_marker("Enter");
            } else if (vk == 0x09) {
                append_marker("Tab");
            } else if (vk == 0x08) {
                annotated_backspace();
            } else if (vk == 0x1B) {
                append_marker("Esc");
            }
            // Ordinary character keys add nothing here; their text arrives as a
            // TextInput event and is appended above.
            break;
        }
        default:
            break;
    }
}

void ReconstructionEngine::process(const Event& event) {
    annotate(event);
    switch (event.type) {
        case EventType::TextInput:
            if (const auto* t = std::get_if<TextInputData>(&event.payload)) {
                insert(utf8_to_utf32(t->text));
            }
            break;
        case EventType::KeyDown:
            handle_key_down(event);
            break;
        case EventType::KeyboardShortcut:
            if (const auto* s = std::get_if<ShortcutData>(&event.payload)) {
                handle_shortcut(*s);
            }
            break;
        case EventType::Paste:
            if (const auto* t = std::get_if<TextInputData>(&event.payload)) {
                insert(utf8_to_utf32(t->text));  // resolved paste text
            } else {
                lower_confidence(Confidence::Medium);  // ref not resolved
            }
            break;
        case EventType::MouseButton:
            if (const auto* m = std::get_if<MouseData>(&event.payload)) {
                if (m->button == MouseButton::Left && m->pressed) {
                    // A click may reposition the caret; we can't know where.
                    state_.cursor_known = false;
                    clear_selection();
                    lower_confidence(Confidence::Medium);
                }
            }
            break;
        default:
            // KeyUp, clipboard-changed, context, session, mouse wheel/move,
            // diagnostics: no effect on the reconstructed buffer.
            break;
    }
}

}  // namespace ir
