#include "security/sensitive_input.hpp"

#include <Windows.h>

#include <string>

namespace ir {

bool password_field_focused() {
    GUITHREADINFO gui{};
    gui.cbSize = sizeof(gui);
    if (!GetGUIThreadInfo(0, &gui) || gui.hwndFocus == nullptr) {
        return false;
    }
    // Standard EDIT (and RichEdit) password fields carry the ES_PASSWORD style.
    const LONG_PTR style = GetWindowLongPtrW(gui.hwndFocus, GWL_STYLE);
    if ((style & ES_PASSWORD) != 0) {
        // Confirm it is an edit-like control (ES_PASSWORD bit is reused by other
        // classes), by checking the window class name prefix.
        wchar_t cls[64] = {};
        GetClassNameW(gui.hwndFocus, cls, 63);
        const std::wstring name(cls);
        if (name.find(L"Edit") != std::wstring::npos ||
            name.find(L"EDIT") != std::wstring::npos ||
            name.find(L"RICHEDIT") != std::wstring::npos ||
            name.find(L"RichEdit") != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

}  // namespace ir
