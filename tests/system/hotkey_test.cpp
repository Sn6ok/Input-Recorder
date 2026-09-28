#include "framework/test_framework.hpp"

#include "system/hotkey.hpp"

TEST_CASE("system.hotkey", "default Ctrl+Shift+R maps to the right modifiers") {
    ir::Hotkey hk{true, true, false, false, 0x52};  // Ctrl+Shift+R
    const std::uint32_t mods = ir::to_win32_modifiers(hk);
    CHECK((mods & ir::kModControl) != 0);
    CHECK((mods & ir::kModShift) != 0);
    CHECK((mods & ir::kModAlt) == 0);
    CHECK((mods & ir::kModWin) == 0);
    CHECK((mods & ir::kModNoRepeat) != 0);  // always no-repeat
}

TEST_CASE("system.hotkey", "all modifiers map through") {
    ir::Hotkey hk{true, true, true, true, 0x41};
    const std::uint32_t mods = ir::to_win32_modifiers(hk);
    CHECK((mods & ir::kModControl) != 0);
    CHECK((mods & ir::kModShift) != 0);
    CHECK((mods & ir::kModAlt) != 0);
    CHECK((mods & ir::kModWin) != 0);
}

TEST_CASE("system.hotkey", "registerable requires a key and a modifier") {
    CHECK(ir::is_registerable(ir::Hotkey{true, true, false, false, 0x52}));
    CHECK(!ir::is_registerable(ir::Hotkey{true, true, false, false, 0}));   // no key
    CHECK(!ir::is_registerable(ir::Hotkey{false, false, false, false, 0x52}));  // no modifier
}
