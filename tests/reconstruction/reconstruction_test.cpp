#include "framework/test_framework.hpp"

#include <string>

#include "reconstruction/reconstruction_engine.hpp"

namespace {

// Virtual-key codes used by the scenarios.
constexpr std::uint16_t kBack = 0x08;
constexpr std::uint16_t kTab = 0x09;
constexpr std::uint16_t kReturn = 0x0D;
constexpr std::uint16_t kEnd = 0x23;
constexpr std::uint16_t kHome = 0x24;
constexpr std::uint16_t kLeft = 0x25;
constexpr std::uint16_t kRight = 0x27;
constexpr std::uint16_t kUp = 0x26;
constexpr std::uint16_t kDelete = 0x2E;

// A small scenario builder: emits normalized events into the engine, mirroring
// what the capture/normalization layers produce at runtime.
struct Harness {
    ir::ReconstructionEngine engine;

    void type(const std::string& utf8) {
        ir::Event e;
        e.type = ir::EventType::TextInput;
        e.payload = ir::TextInputData{utf8};
        engine.process(e);
    }
    void key(std::uint16_t vk, ir::EventFlags mods = ir::EventFlags::None) {
        ir::Event e;
        e.type = ir::EventType::KeyDown;
        e.flags = mods;
        e.payload = ir::KeyEventData{vk, 0};
        engine.process(e);
    }
    void shortcut(std::uint16_t key, ir::EventFlags mods) {
        ir::Event e;
        e.type = ir::EventType::KeyboardShortcut;
        e.payload = ir::ShortcutData{static_cast<std::uint32_t>(mods), key, ""};
        engine.process(e);
    }
    void paste(const std::string& utf8) {
        ir::Event e;
        e.type = ir::EventType::Paste;
        e.payload = ir::TextInputData{utf8};
        engine.process(e);
    }
    void left_click() {
        ir::Event e;
        e.type = ir::EventType::MouseButton;
        e.payload = ir::MouseData{ir::MouseButton::Left, true, 0, 0, 0};
        engine.process(e);
    }
    std::string text() const { return engine.text(); }
};

const ir::EventFlags kCtrl = ir::EventFlags::ModCtrl;
const ir::EventFlags kShift = ir::EventFlags::ModShift;

}  // namespace

// ---- golden: basic typing (§481) ----------------------------------------
TEST_CASE("recon.golden", "basic typing char by char -> Hello") {
    Harness h;
    h.type("H"); h.type("e"); h.type("l"); h.type("l"); h.type("o");
    CHECK_EQ(h.text(), std::string("Hello"));
    CHECK(h.engine.confidence() == ir::Confidence::High);
}

// ---- golden: backspace (§482) --------------------------------------------
TEST_CASE("recon.golden", "backspace removes last char") {
    Harness h;
    h.type("Hello");
    h.key(kBack);
    CHECK_EQ(h.text(), std::string("Hell"));
}

// ---- golden: enter (§484) ------------------------------------------------
TEST_CASE("recon.golden", "enter inserts newline") {
    Harness h;
    h.type("Hello");
    h.key(kReturn);
    h.type("World");
    CHECK_EQ(h.text(), std::string("Hello\nWorld"));
}

// ---- golden: space (§486) ------------------------------------------------
TEST_CASE("recon.golden", "space between words") {
    Harness h;
    h.type("Hello"); h.type(" "); h.type("World");
    CHECK_EQ(h.text(), std::string("Hello World"));
}

// ---- golden: tab (§485) --------------------------------------------------
TEST_CASE("recon.golden", "tab inserts a tab char") {
    Harness h;
    h.type("a");
    h.key(kTab);
    h.type("b");
    CHECK_EQ(h.text(), std::string("a\tb"));
}

// ---- golden: key repeat (§489) -------------------------------------------
TEST_CASE("recon.golden", "repeated A produces AAAAA") {
    Harness h;
    for (int i = 0; i < 5; ++i) h.type("A");
    CHECK_EQ(h.text(), std::string("AAAAA"));
}

// ---- golden: insertion mid-buffer via cursor -----------------------------
TEST_CASE("recon.golden", "insert in the middle with cursor movement") {
    Harness h;
    h.type("Hllo");
    h.key(kLeft); h.key(kLeft); h.key(kLeft);  // caret after 'H'
    h.type("e");
    CHECK_EQ(h.text(), std::string("Hello"));
}

// ---- golden: delete with cursor (§483) -----------------------------------
TEST_CASE("recon.golden", "delete forward at cursor") {
    Harness h;
    h.type("Hello");
    h.key(kLeft); h.key(kLeft);  // "Hel|lo"
    h.key(kDelete);              // removes the 'l' at the caret
    CHECK_EQ(h.text(), std::string("Helo"));
}

// ---- golden: shortcuts do not become literal text (§488) -----------------
TEST_CASE("recon.golden", "Ctrl+C/V/X/Z never insert letters") {
    Harness h;
    h.type("hi");
    h.shortcut('C', kCtrl);
    h.shortcut('V', kCtrl);
    h.shortcut('X', kCtrl);  // no selection -> no-op
    CHECK_EQ(h.text(), std::string("hi"));
    h.shortcut('Z', kCtrl);  // undo -> confidence drops, text unchanged
    CHECK_EQ(h.text(), std::string("hi"));
    CHECK(h.engine.confidence() == ir::Confidence::Low);
}

// ---- golden: Ctrl+A + Backspace clears text (§75) ------------------------
TEST_CASE("recon.golden", "Ctrl+A then Backspace clears the buffer") {
    Harness h;
    h.type("a long prompt the user wrote");
    h.shortcut('A', kCtrl);
    h.key(kBack);
    CHECK_EQ(h.text(), std::string(""));
    CHECK(h.engine.confidence() == ir::Confidence::High);  // known exactly
}

// ---- golden: Ctrl+X cuts the selection -----------------------------------
TEST_CASE("recon.golden", "Ctrl+A then Ctrl+X removes all") {
    Harness h;
    h.type("delete me");
    h.shortcut('A', kCtrl);
    h.shortcut('X', kCtrl);
    CHECK_EQ(h.text(), std::string(""));
}

// ---- golden: shift+arrow selection replaced by typing --------------------
TEST_CASE("recon.golden", "shift-left selection replaced by typed text") {
    Harness h;
    h.type("Hello");
    h.key(kLeft, kShift);  // select "o"
    h.key(kLeft, kShift);  // select "lo"
    h.type("p");           // replace "lo" -> "Help"
    CHECK_EQ(h.text(), std::string("Help"));
}

// ---- golden: paste (§494 paste) ------------------------------------------
TEST_CASE("recon.golden", "paste inserts clipboard text") {
    Harness h;
    h.type("A");
    h.paste("BC");
    h.type("D");
    CHECK_EQ(h.text(), std::string("ABCD"));
}

// ---- golden: unicode (§490) ----------------------------------------------
TEST_CASE("recon.golden", "unicode text is preserved") {
    const std::string uk = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD1\x96\xD1\x82";  // Привіт
    const std::string emoji = "\xF0\x9F\x98\x80";                              // 😀
    Harness h;
    h.type(uk);
    h.type(" ");
    h.type(emoji);
    CHECK_EQ(h.text(), uk + " " + emoji);
}

TEST_CASE("recon.golden", "backspace removes a whole emoji code point") {
    const std::string emoji = "\xF0\x9F\x98\x80";
    Harness h;
    h.type("ab");
    h.type(emoji);
    h.key(kBack);  // must remove the full emoji, not a byte
    CHECK_EQ(h.text(), std::string("ab"));
}

// ---- golden: layout switch (§493) ----------------------------------------
TEST_CASE("recon.golden", "layout switch keeps both segments") {
    const std::string uk = "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD1\x96\xD1\x82";
    Harness h;
    h.type("Hello");
    h.type(uk);  // as if the layout switched to Ukrainian
    CHECK_EQ(h.text(), "Hello" + uk);
}

// ---- Home/End line navigation --------------------------------------------
TEST_CASE("recon.golden", "Home moves to start of current line") {
    Harness h;
    h.type("abc");
    h.key(kReturn);
    h.type("def");           // buffer: "abc\ndef", caret at end
    h.key(kHome);            // -> start of "def"
    h.type("X");             // "abc\nXdef"
    CHECK_EQ(h.text(), std::string("abc\nXdef"));
    h.key(kEnd);             // -> end of the "Xdef" line
    h.type("!");             // "abc\nXdef!"
    CHECK_EQ(h.text(), std::string("abc\nXdef!"));
}

// ---- uncertainty: vertical movement & mouse click ------------------------
TEST_CASE("recon.uncertainty", "arrow up marks caret unknown, lowers confidence") {
    Harness h;
    h.type("line");
    h.key(kUp);
    CHECK(!h.engine.cursor_known());
    CHECK(h.engine.confidence() == ir::Confidence::Medium);
    // Text still appended (best effort) but flagged uncertain.
    h.type("X");
    CHECK_EQ(h.text(), std::string("lineX"));
}

TEST_CASE("recon.uncertainty", "mouse click makes caret position unknown") {
    Harness h;
    h.type("hello");
    h.left_click();
    CHECK(!h.engine.cursor_known());
    CHECK(h.engine.confidence() == ir::Confidence::Medium);
}

// ---- annotated view: special keys & shortcuts as inline markers ----------
TEST_CASE("recon.annotated", "special keys and shortcuts appear as markers") {
    Harness h;
    h.type("ab");
    h.key(kReturn);            // Enter
    h.type("cd");
    h.key(kTab);               // Tab
    h.key('S', kCtrl | kShift);  // Ctrl+Shift+S shortcut (KeyDown + modifiers)

    // Clean recovered text (Copy All) is unaffected by annotation.
    CHECK_EQ(h.text(), std::string("ab\ncd\t"));

    // The annotated view carries the typed text plus key/shortcut markers.
    const std::string ann = h.engine.annotated_text();
    CHECK(ann.find("ab") != std::string::npos);
    CHECK(ann.find("cd") != std::string::npos);
    CHECK(ann.find("(Enter)") != std::string::npos);
    CHECK(ann.find("(Tab)") != std::string::npos);
    CHECK(ann.find("(Ctrl + Shift + S)") != std::string::npos);
}

TEST_CASE("recon.annotated", "backspace trims typed text in the annotated view") {
    Harness h;
    h.type("hello");
    h.key(kBack);
    CHECK_EQ(h.engine.annotated_text(), std::string("hell"));
}

TEST_CASE("recon.annotated", "backspace over a multi-byte char keeps valid UTF-8") {
    Harness h;
    h.type("\xD0\xB0\xD0\xB1");  // "аб" — two 2-byte Cyrillic letters
    h.key(kBack);               // delete "б"
    const std::string ann = h.engine.annotated_text();
    CHECK_EQ(ann, std::string("\xD0\xB0"));                 // "а", intact
    CHECK(ann.find("\xEF\xBF\xBD") == std::string::npos);  // no U+FFFD square
}

// ---- snapshot / restore (point-in-time, §211) ----------------------------
TEST_CASE("recon.snapshot", "snapshot and restore round-trips state") {
    Harness h;
    h.type("first version");
    ir::TextState snap = h.engine.snapshot();

    h.shortcut('A', kCtrl);
    h.key(kBack);
    CHECK_EQ(h.text(), std::string(""));

    h.engine.restore(snap);
    CHECK_EQ(h.text(), std::string("first version"));
}
