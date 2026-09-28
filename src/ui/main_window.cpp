#include "ui/main_window.hpp"

#include <Windows.h>
#include <dwmapi.h>

#include "ui/history_formatting.hpp"
#include "utils/unicode.hpp"

namespace ir {
namespace {

constexpr wchar_t kClassName[] = L"InputRecorderMainWindow";

constexpr int IDC_STATUS = 1001;
constexpr int IDC_NOTE = 1002;
constexpr int IDC_TEXT = 1003;
constexpr int IDC_TOGGLE = 1004;
constexpr int IDC_COPY = 1005;
constexpr int IDC_HISTORY = 1006;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
#endif

std::wstring to_w(const std::string& utf8) {
    std::u16string u16 = utf8_to_utf16(utf8);
    return std::wstring(reinterpret_cast<const wchar_t*>(u16.data()), u16.size());
}

HFONT ui_font() {
    return static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
}

}  // namespace

MainWindow::~MainWindow() {
    if (bg_brush_ != nullptr) DeleteObject(static_cast<HBRUSH>(bg_brush_));
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self =
        reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return static_cast<LRESULT>(self->handle_message(
            hwnd, msg, static_cast<unsigned long long>(wparam),
            static_cast<long long>(lparam)));
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool MainWindow::create(void* hinstance, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    HINSTANCE inst = static_cast<HINSTANCE>(hinstance);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MainWindowProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);  // ERROR_CLASS_ALREADY_EXISTS is fine

    const std::wstring title = to_w("Input Recorder");
    HWND hwnd = CreateWindowExW(
        0, kClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, 640, 480, nullptr, nullptr, inst, this);
    hwnd_ = hwnd;
    return hwnd != nullptr;
}

void MainWindow::show(int show_command) {
    if (hwnd_ != nullptr) {
        ShowWindow(static_cast<HWND>(hwnd_), show_command);
        UpdateWindow(static_cast<HWND>(hwnd_));
    }
}

long long MainWindow::handle_message(void* hwnd_v, unsigned msg,
                                     unsigned long long wparam,
                                     long long lparam) {
    HWND hwnd = static_cast<HWND>(hwnd_v);
    switch (msg) {
        case WM_CREATE:
            on_create();
            return 0;
        case WM_SIZE:
            on_size(LOWORD(lparam), HIWORD(lparam));
            return 0;
        case WM_COMMAND:
            on_command(LOWORD(wparam));
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            const COLORREF text = dark_ ? RGB(240, 240, 240) : RGB(20, 20, 20);
            const COLORREF back = dark_ ? RGB(32, 32, 32) : RGB(255, 255, 255);
            // The recording indicator gets an accent colour.
            if (reinterpret_cast<HWND>(lparam) == status_label_) {
                SetTextColor(dc, is_recording(model_.status())
                                     ? RGB(220, 60, 60)
                                     : RGB(150, 150, 150));
            } else {
                SetTextColor(dc, text);
            }
            SetBkColor(dc, back);
            return reinterpret_cast<long long>(bg_brush_);
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, static_cast<WPARAM>(wparam),
                          static_cast<LPARAM>(lparam));
}

void MainWindow::on_create() {
    HWND hwnd = static_cast<HWND>(hwnd_);
    HINSTANCE inst =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
    HFONT font = ui_font();

    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                    int id) -> HWND {
        HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0,
                                 0, 0, hwnd, reinterpret_cast<HMENU>(
                                                  static_cast<INT_PTR>(id)),
                                 inst, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    };

    status_label_ = make(L"STATIC", L"", SS_LEFTNOWORDWRAP, IDC_STATUS);
    note_label_ = make(L"STATIC", L"", SS_LEFTNOWORDWRAP, IDC_NOTE);
    text_view_ = make(L"EDIT", L"",
                      ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL |
                          WS_BORDER,
                      IDC_TEXT);
    toggle_btn_ = make(L"BUTTON", L"Pause", BS_PUSHBUTTON, IDC_TOGGLE);
    copy_btn_ = make(L"BUTTON", L"Copy All", BS_PUSHBUTTON, IDC_COPY);
    history_btn_ = make(L"BUTTON", L"History", BS_PUSHBUTTON, IDC_HISTORY);

    apply_theme();
    refresh_status();
    refresh_text();
}

void MainWindow::on_size(int width, int height) {
    const int pad = 12;
    const int line = 22;
    const int btn_h = 30;
    const int btn_w = 110;

    int y = pad;
    MoveWindow(static_cast<HWND>(status_label_), pad, y, width - 2 * pad, line,
               TRUE);
    y += line + 4;
    MoveWindow(static_cast<HWND>(note_label_), pad, y, width - 2 * pad, line, TRUE);
    y += line + 6;

    const int text_bottom = height - pad - btn_h - pad;
    MoveWindow(static_cast<HWND>(text_view_), pad, y, width - 2 * pad,
               (text_bottom > y) ? text_bottom - y : 0, TRUE);

    const int by = height - pad - btn_h;
    MoveWindow(static_cast<HWND>(toggle_btn_), pad, by, btn_w, btn_h, TRUE);
    MoveWindow(static_cast<HWND>(copy_btn_), pad + btn_w + 8, by, btn_w, btn_h,
               TRUE);
    MoveWindow(static_cast<HWND>(history_btn_), pad + 2 * (btn_w + 8), by, btn_w,
               btn_h, TRUE);
}

void MainWindow::on_command(int control_id) {
    switch (control_id) {
        case IDC_TOGGLE: {
            const RecordingStatus s = model_.toggle_status();
            refresh_status();
            if (callbacks_.on_recording_changed)
                callbacks_.on_recording_changed(is_recording(s));
            break;
        }
        case IDC_COPY:
            do_copy_all();
            break;
        case IDC_HISTORY:
            if (callbacks_.on_open_history) callbacks_.on_open_history();
            break;
        default:
            break;
    }
}

void MainWindow::refresh_status() {
    if (status_label_ == nullptr) return;
    SetWindowTextW(static_cast<HWND>(status_label_),
                   to_w(model_.status_indicator()).c_str());
    SetWindowTextW(static_cast<HWND>(toggle_btn_),
                   is_recording(model_.status()) ? L"Pause" : L"Resume");
    // Repaint the indicator so its accent colour updates.
    InvalidateRect(static_cast<HWND>(status_label_), nullptr, TRUE);
}

void MainWindow::refresh_text() {
    if (text_view_ == nullptr) return;
    SetWindowTextW(static_cast<HWND>(text_view_),
                   to_w(model_.display_text()).c_str());
    SetWindowTextW(static_cast<HWND>(note_label_),
                   to_w(model_.confidence_note()).c_str());
}

void MainWindow::do_copy_all() {
    const std::string text = model_.copy_all_text();
    if (text.empty()) return;

    // Tell the app first so the clipboard monitor suppresses this self-copy.
    if (callbacks_.on_copy_all) callbacks_.on_copy_all(text);

    if (!OpenClipboard(static_cast<HWND>(hwnd_))) return;
    EmptyClipboard();
    const std::wstring w = to_w(text);
    const SIZE_T bytes = (w.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem != nullptr) {
        void* dst = GlobalLock(mem);
        if (dst != nullptr) {
            memcpy(dst, w.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

void MainWindow::apply_theme() {
    if (bg_brush_ != nullptr) {
        DeleteObject(static_cast<HBRUSH>(bg_brush_));
        bg_brush_ = nullptr;
    }
    bg_brush_ = CreateSolidBrush(dark_ ? RGB(32, 32, 32) : RGB(255, 255, 255));

    const BOOL dark = dark_ ? TRUE : FALSE;
    DwmSetWindowAttribute(static_cast<HWND>(hwnd_), DWMWA_USE_IMMERSIVE_DARK_MODE,
                          &dark, sizeof(dark));
    InvalidateRect(static_cast<HWND>(hwnd_), nullptr, TRUE);
}

void MainWindow::set_status(RecordingStatus status) {
    model_.set_status(status);
    refresh_status();
}

void MainWindow::set_reconstruction(const std::string& text,
                                    Confidence confidence) {
    model_.set_reconstruction(text, confidence);
    refresh_text();
}

void MainWindow::set_dark_theme(bool dark) {
    dark_ = dark;
    if (hwnd_ != nullptr) apply_theme();
}

int MainWindow::run_message_loop() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(static_cast<HWND>(hwnd_), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

}  // namespace ir
