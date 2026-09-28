#pragma once

// Turns a normalized event stream into reconstructed text (spec §31, §69-§77,
// §95). OS-independent and fully unit-testable.
//
// Input contract:
//   * TextInput events         -> insert their resolved characters
//   * KeyDown events           -> editing/navigation keys (Backspace, Delete,
//                                 Enter, Tab, arrows, Home, End) are applied;
//                                 printable keys are ignored here because their
//                                 characters arrive as TextInput (spec §68)
//   * KeyboardShortcut events  -> Ctrl+A/C/X/Z etc. (never inserted as text)
//   * Paste events             -> insert resolved clipboard text if attached
//   * MouseButton (left press) -> caret position becomes unknown
//
// The engine never fabricates text: when it cannot determine an exact result it
// lowers the confidence and marks uncertainty instead of guessing (spec §72,
// §90, §91).

#include <string>

#include "core/confidence.hpp"
#include "core/event.hpp"
#include "reconstruction/text_state.hpp"

namespace ir {

class ReconstructionEngine {
public:
    void process(const Event& event);

    std::string text() const { return state_.text_utf8(); }
    const std::u32string& code_points() const { return state_.buffer; }
    Confidence confidence() const { return state_.confidence; }
    std::size_t cursor() const { return state_.cursor; }
    bool cursor_known() const { return state_.cursor_known; }
    bool selection_active() const { return state_.selection_active; }

    // An annotated, append-style rendering of the input for the live view: the
    // typed text with inline markers for special keys and shortcuts, e.g.
    // "hello(Enter)\n(Ctrl + Shift + S)\nworld". Separate from text() so Copy All
    // still yields clean, pasteable recovered text.
    const std::string& annotated_text() const { return annotated_; }

    void reset() {
        state_ = TextState{};
        annotated_.clear();
    }

    TextState snapshot() const { return state_; }
    void restore(const TextState& s) { state_ = s; }

private:
    void insert(const std::u32string& text);
    void backspace();
    void delete_forward();
    void delete_selection();
    void clear_selection();
    void move_left(bool select);
    void move_right(bool select);
    void move_home(bool select);
    void move_end(bool select);
    void move_vertical();
    void word_move(bool right, bool select);
    void select_all();
    void begin_selection_if_needed(bool select);
    void lower_confidence(Confidence c);

    void handle_key_down(const Event& event);
    void handle_shortcut(const ShortcutData& shortcut);

    // Builds the annotated view (does not touch the clean buffer).
    void annotate(const Event& event);
    void append_marker(const std::string& label);
    void annotated_backspace();

    TextState state_;
    std::string annotated_;  // append-style annotated view (UTF-8)
};

}  // namespace ir
