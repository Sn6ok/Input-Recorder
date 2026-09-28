#include "ui/os_theme.hpp"

#include <Windows.h>

namespace ir {

bool system_prefers_dark() {
    DWORD value = 1;  // default: light
    DWORD size = sizeof(value);
    const LSTATUS rc = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    if (rc != ERROR_SUCCESS) return false;
    return value == 0;  // 0 == dark apps
}

}  // namespace ir
