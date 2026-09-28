#include "ui/tray_icon.hpp"

#include <Windows.h>
#include <shellapi.h>

#include <string>

#include "ui/tray_menu_model.hpp"
#include "utils/unicode.hpp"

namespace ir {
namespace {

constexpr wchar_t kClassName[] = L"InputRecorderTrayWindow";
constexpr UINT WM_TRAYCALLBACK = WM_APP + 1;
constexpr UINT kTrayId = 1;

enum : int {
    IDM_TOGGLE = 4101,
    IDM_SHOWHIDE = 4102,
    IDM_SETTINGS = 4103,
    IDM_EXIT = 4104,
};

std::wstring to_w(const std::string& utf8) {
    std::u16string u16 = utf8_to_utf16(utf8);
    return std::wstring(reinterpret_cast<const wchar_t*>(u16.data()), u16.size());
}

void copy_tip(NOTIFYICONDATAW& nid, const std::string& tip) {
    const std::wstring w = to_w(tip);
    const size_t n = w.size() < 127 ? w.size() : 127;
    wmemcpy(nid.szTip, w.c_str(), n);
    nid.szTip[n] = L'\0';
}

}  // namespace

LRESULT CALLBACK TrayWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<TrayIcon*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return static_cast<LRESULT>(self->handle_message(
            hwnd, msg, static_cast<unsigned long long>(wparam),
            static_cast<long long>(lparam)));
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

TrayIcon::~TrayIcon() { destroy(); }

RecordingStatus TrayIcon::current_status() const {
    return cb_.status ? cb_.status() : RecordingStatus::Recording;
}

bool TrayIcon::create(void* hinstance, Callbacks callbacks) {
    cb_ = std::move(callbacks);
    hinstance_ = hinstance;
    HINSTANCE inst = static_cast<HINSTANCE>(hinstance);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &TrayWindowProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                nullptr, inst, this);
    hwnd_ = hwnd;
    if (hwnd == nullptr) return false;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYCALLBACK;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    copy_tip(nid, tray_menu(current_status(), false).tooltip);
    added_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    return added_;
}

void TrayIcon::destroy() {
    if (added_ && hwnd_ != nullptr) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = static_cast<HWND>(hwnd_);
        nid.uID = kTrayId;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        added_ = false;
    }
    if (hwnd_ != nullptr) {
        DestroyWindow(static_cast<HWND>(hwnd_));
        hwnd_ = nullptr;
    }
}

void TrayIcon::refresh() {
    if (!added_ || hwnd_ == nullptr) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = static_cast<HWND>(hwnd_);
    nid.uID = kTrayId;
    nid.uFlags = NIF_TIP;
    copy_tip(nid, tray_menu(current_status(), false).tooltip);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayIcon::show_menu() {
    HWND hwnd = static_cast<HWND>(hwnd_);
    const bool visible = cb_.window_visible && cb_.window_visible();
    const TrayMenu labels = tray_menu(current_status(), visible);

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return;
    AppendMenuW(menu, MF_STRING, IDM_TOGGLE, to_w(labels.toggle_label).c_str());
    AppendMenuW(menu, MF_STRING, IDM_SHOWHIDE, to_w(labels.show_hide_label).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, to_w(labels.settings_label).c_str());
    AppendMenuW(menu, MF_STRING, IDM_EXIT, to_w(labels.exit_label).c_str());

    POINT pt;
    GetCursorPos(&pt);
    // Required so the menu dismisses correctly when clicking elsewhere.
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

long long TrayIcon::handle_message(void* hwnd_v, unsigned msg,
                                   unsigned long long wparam, long long lparam) {
    HWND hwnd = static_cast<HWND>(hwnd_v);
    if (msg == WM_TRAYCALLBACK) {
        switch (LOWORD(lparam)) {
            case WM_LBUTTONDBLCLK:
                if (cb_.on_show_hide) cb_.on_show_hide();
                break;
            case WM_RBUTTONUP:
            case WM_CONTEXTMENU:
                show_menu();
                break;
            default:
                break;
        }
        return 0;
    }
    if (msg == WM_COMMAND) {
        switch (LOWORD(wparam)) {
            case IDM_TOGGLE:
                if (cb_.on_toggle_recording) cb_.on_toggle_recording();
                break;
            case IDM_SHOWHIDE:
                if (cb_.on_show_hide) cb_.on_show_hide();
                break;
            case IDM_SETTINGS:
                if (cb_.on_settings) cb_.on_settings();
                break;
            case IDM_EXIT:
                if (cb_.on_exit) cb_.on_exit();
                break;
            default:
                break;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, static_cast<WPARAM>(wparam),
                          static_cast<LPARAM>(lparam));
}

}  // namespace ir
