#include "ui/settings_window.hpp"

#include <Windows.h>

#include <charconv>
#include <string>

#include "utils/unicode.hpp"

namespace ir {
namespace {

constexpr wchar_t kClassName[] = L"InputRecorderSettingsWindow";

enum : int {
    IDC_KEYBOARD = 3101,
    IDC_CLICKS,
    IDC_WHEEL,
    IDC_MOVEMENT,
    IDC_WINDOW,
    IDC_CLIPBOARD,
    IDC_STARTUP,
    IDC_TRAY,
    IDC_ONTOP,
    IDC_NOTIFY,
    IDC_THEME,
    IDC_RETENTION,
    IDC_SAVE,
    IDC_CANCEL,
};

HWND make_check(HWND parent, HINSTANCE inst, HFONT font, const wchar_t* label,
                int id, int x, int y, int w) {
    HWND h = CreateWindowExW(0, L"BUTTON", label,
                             WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, x, y, w, 22,
                             parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                             inst, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    return h;
}

void set_check(HWND parent, int id, bool on) {
    SendMessageW(GetDlgItem(parent, id), BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

bool get_check(HWND parent, int id) {
    return SendMessageW(GetDlgItem(parent, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

}  // namespace

LRESULT CALLBACK SettingsWindowProc(HWND hwnd, UINT msg, WPARAM wparam,
                                    LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self =
        reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return static_cast<LRESULT>(self->handle_message(
            hwnd, msg, static_cast<unsigned long long>(wparam),
            static_cast<long long>(lparam)));
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool SettingsWindow::create(void* hinstance, void* owner_hwnd,
                            const Configuration& initial, OnSave on_save) {
    config_ = initial;
    on_save_ = std::move(on_save);
    HINSTANCE inst = static_cast<HINSTANCE>(hinstance);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindowProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClassName, L"Settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, 420,
        460, static_cast<HWND>(owner_hwnd), nullptr, inst, this);
    hwnd_ = hwnd;
    return hwnd != nullptr;
}

void SettingsWindow::show(int show_command) {
    if (hwnd_ != nullptr) {
        ShowWindow(static_cast<HWND>(hwnd_), show_command);
        UpdateWindow(static_cast<HWND>(hwnd_));
    }
}

long long SettingsWindow::handle_message(void* hwnd_v, unsigned msg,
                                         unsigned long long wparam,
                                         long long lparam) {
    HWND hwnd = static_cast<HWND>(hwnd_v);
    switch (msg) {
        case WM_CREATE:
            on_create();
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            if (id == IDC_SAVE) {
                config_ = read_controls();
                if (on_save_) on_save_(config_);
                DestroyWindow(hwnd);
            } else if (id == IDC_CANCEL) {
                DestroyWindow(hwnd);
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, static_cast<WPARAM>(wparam),
                          static_cast<LPARAM>(lparam));
}

void SettingsWindow::on_create() {
    HWND hwnd = static_cast<HWND>(hwnd_);
    HINSTANCE inst =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    int y = 12;
    const int x = 16, w = 360, step = 26;
    make_check(hwnd, inst, font, L"Record keyboard", IDC_KEYBOARD, x, y, w); y += step;
    make_check(hwnd, inst, font, L"Record mouse clicks", IDC_CLICKS, x, y, w); y += step;
    make_check(hwnd, inst, font, L"Record mouse wheel", IDC_WHEEL, x, y, w); y += step;
    make_check(hwnd, inst, font, L"Record mouse movement", IDC_MOVEMENT, x, y, w); y += step;
    make_check(hwnd, inst, font, L"Track active window", IDC_WINDOW, x, y, w); y += step;
    make_check(hwnd, inst, font, L"Monitor clipboard", IDC_CLIPBOARD, x, y, w); y += step;
    make_check(hwnd, inst, font, L"Show notifications", IDC_NOTIFY, x, y, w); y += step;
    make_check(hwnd, inst, font, L"Start with Windows", IDC_STARTUP, x, y, w); y += step;
    make_check(hwnd, inst, font, L"Minimize to tray", IDC_TRAY, x, y, w); y += step;
    make_check(hwnd, inst, font, L"Always on top", IDC_ONTOP, x, y, w); y += step;

    y += 6;
    HWND lblTheme = CreateWindowExW(0, L"STATIC", L"Theme:", WS_CHILD | WS_VISIBLE,
                                    x, y + 4, 60, 20, hwnd, nullptr, inst, nullptr);
    SendMessageW(lblTheme, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    HWND theme = CreateWindowExW(0, L"COMBOBOX", L"",
                                 WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                 x + 70, y, 160, 120, hwnd,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_THEME)),
                                 inst, nullptr);
    SendMessageW(theme, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"System"));
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Light"));
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Dark"));
    y += 32;

    HWND lblRet = CreateWindowExW(0, L"STATIC", L"Retention (days, 0=forever):",
                                  WS_CHILD | WS_VISIBLE, x, y + 4, 200, 20, hwnd,
                                  nullptr, inst, nullptr);
    SendMessageW(lblRet, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    HWND ret = CreateWindowExW(0, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | WS_BORDER | ES_NUMBER, x + 210,
                               y, 70, 22, hwnd,
                               reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_RETENTION)),
                               inst, nullptr);
    SendMessageW(ret, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    y += 40;

    HWND save = CreateWindowExW(0, L"BUTTON", L"Save",
                                WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, x, y, 90, 30,
                                hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SAVE)),
                                inst, nullptr);
    SendMessageW(save, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    HWND cancel = CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE,
                                  x + 100, y, 90, 30, hwnd,
                                  reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CANCEL)),
                                  inst, nullptr);
    SendMessageW(cancel, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);

    load_controls();
}

void SettingsWindow::load_controls() {
    HWND hwnd = static_cast<HWND>(hwnd_);
    set_check(hwnd, IDC_KEYBOARD, config_.record_keyboard);
    set_check(hwnd, IDC_CLICKS, config_.record_mouse_clicks);
    set_check(hwnd, IDC_WHEEL, config_.record_mouse_wheel);
    set_check(hwnd, IDC_MOVEMENT, config_.record_mouse_movement);
    set_check(hwnd, IDC_WINDOW, config_.track_active_window);
    set_check(hwnd, IDC_CLIPBOARD, config_.monitor_clipboard);
    set_check(hwnd, IDC_NOTIFY, config_.show_notifications);
    set_check(hwnd, IDC_STARTUP, config_.start_with_windows);
    set_check(hwnd, IDC_TRAY, config_.minimize_to_tray);
    set_check(hwnd, IDC_ONTOP, config_.always_on_top);

    int theme_index = 0;  // System
    if (config_.theme == Theme::Light) theme_index = 1;
    else if (config_.theme == Theme::Dark) theme_index = 2;
    SendMessageW(GetDlgItem(hwnd, IDC_THEME), CB_SETCURSEL, theme_index, 0);

    SetWindowTextW(GetDlgItem(hwnd, IDC_RETENTION),
                   std::to_wstring(config_.retention_days).c_str());
}

Configuration SettingsWindow::read_controls() {
    HWND hwnd = static_cast<HWND>(hwnd_);
    Configuration c = config_;  // preserve fields not exposed here
    c.record_keyboard = get_check(hwnd, IDC_KEYBOARD);
    c.record_mouse_clicks = get_check(hwnd, IDC_CLICKS);
    c.record_mouse_wheel = get_check(hwnd, IDC_WHEEL);
    c.record_mouse_movement = get_check(hwnd, IDC_MOVEMENT);
    c.track_active_window = get_check(hwnd, IDC_WINDOW);
    c.monitor_clipboard = get_check(hwnd, IDC_CLIPBOARD);
    c.show_notifications = get_check(hwnd, IDC_NOTIFY);
    c.start_with_windows = get_check(hwnd, IDC_STARTUP);
    c.minimize_to_tray = get_check(hwnd, IDC_TRAY);
    c.always_on_top = get_check(hwnd, IDC_ONTOP);

    switch (SendMessageW(GetDlgItem(hwnd, IDC_THEME), CB_GETCURSEL, 0, 0)) {
        case 1: c.theme = Theme::Light; break;
        case 2: c.theme = Theme::Dark; break;
        default: c.theme = Theme::System; break;
    }

    wchar_t buf[16] = {};
    GetWindowTextW(GetDlgItem(hwnd, IDC_RETENTION), buf, 16);
    int days = c.retention_days;
    std::wstring w(buf);
    std::string narrow(w.begin(), w.end());  // digits only (ES_NUMBER)
    std::from_chars(narrow.data(), narrow.data() + narrow.size(), days);
    c.retention_days = days;

    validate(c);
    return c;
}

}  // namespace ir
