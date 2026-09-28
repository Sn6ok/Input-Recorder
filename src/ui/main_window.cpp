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
constexpr int IDC_SETTINGS = 1007;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
#endif

// --- Theme palette --------------------------------------------------------
COLORREF col_window(bool d) { return d ? RGB(30, 30, 30) : RGB(243, 243, 243); }
COLORREF col_panel(bool d) { return d ? RGB(37, 37, 38) : RGB(255, 255, 255); }
COLORREF col_text(bool d) { return d ? RGB(235, 235, 235) : RGB(28, 28, 28); }
COLORREF col_subtext(bool d) { return d ? RGB(155, 155, 155) : RGB(110, 110, 110); }
COLORREF col_button(bool d) { return d ? RGB(55, 55, 57) : RGB(228, 228, 228); }
COLORREF col_button_pressed(bool d) { return d ? RGB(75, 75, 78) : RGB(206, 206, 206); }
constexpr COLORREF kAccent = RGB(0, 120, 215);         // primary button
constexpr COLORREF kAccentPressed = RGB(0, 99, 177);
constexpr COLORREF kRecording = RGB(226, 74, 74);      // ● RECORDING
constexpr COLORREF kPaused = RGB(150, 150, 150);       // ○ PAUSED

std::wstring to_w(const std::string& utf8) {
    std::u16string u16 = utf8_to_utf16(utf8);
    return std::wstring(reinterpret_cast<const wchar_t*>(u16.data()), u16.size());
}

HFONT make_font(int point_size, int weight) {
    HDC screen = GetDC(nullptr);
    const int height = -MulDiv(point_size, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(nullptr, screen);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       VARIABLE_PITCH, L"Segoe UI");
}

}  // namespace

MainWindow::~MainWindow() {
    if (bg_brush_ != nullptr) DeleteObject(static_cast<HBRUSH>(bg_brush_));
    if (panel_brush_ != nullptr) DeleteObject(static_cast<HBRUSH>(panel_brush_));
    if (body_font_ != nullptr) DeleteObject(static_cast<HFONT>(body_font_));
    if (title_font_ != nullptr) DeleteObject(static_cast<HFONT>(title_font_));
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
        CW_USEDEFAULT, 720, 540, nullptr, nullptr, inst, this);
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
    // Keep the member handle valid even during WM_CREATE, which fires *inside*
    // CreateWindowEx before create() has assigned hwnd_. Without this, on_create
    // would parent its child controls to a null window and they would all fail
    // to be created (a blank window).
    hwnd_ = hwnd;
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
        case WM_DRAWITEM:
            draw_button(reinterpret_cast<void*>(lparam));
            return 1;
        case WM_ERASEBKGND: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect(dc, &rc, static_cast<HBRUSH>(bg_brush_));
            return 1;
        }
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            if (reinterpret_cast<HWND>(lparam) == status_label_) {
                SetTextColor(dc, is_recording(model_.status()) ? kRecording : kPaused);
            } else {
                SetTextColor(dc, col_subtext(dark_));
            }
            SetBkColor(dc, col_window(dark_));
            return reinterpret_cast<long long>(bg_brush_);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, col_text(dark_));
            SetBkColor(dc, col_panel(dark_));
            return reinterpret_cast<long long>(panel_brush_);
        }
        case WM_CLOSE:
            if (close_to_tray_) {
                ShowWindow(hwnd, SW_HIDE);
            } else {
                PostQuitMessage(0);
            }
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

    body_font_ = make_font(10, FW_NORMAL);
    title_font_ = make_font(15, FW_SEMIBOLD);

    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                    int id, HFONT font) -> HWND {
        HWND h = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0,
                                 0, 0, hwnd, reinterpret_cast<HMENU>(
                                                  static_cast<INT_PTR>(id)),
                                 inst, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return h;
    };

    HFONT body = static_cast<HFONT>(body_font_);
    HFONT title = static_cast<HFONT>(title_font_);

    status_label_ = make(L"STATIC", L"", SS_LEFTNOWORDWRAP, IDC_STATUS, title);
    note_label_ = make(L"STATIC", L"", SS_LEFTNOWORDWRAP, IDC_NOTE, body);
    text_view_ = make(L"EDIT", L"",
                      ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                      IDC_TEXT, body);
    // Owner-drawn, flat, themed buttons.
    toggle_btn_ = make(L"BUTTON", L"Pause", BS_OWNERDRAW, IDC_TOGGLE, body);
    copy_btn_ = make(L"BUTTON", L"Copy All", BS_OWNERDRAW, IDC_COPY, body);
    history_btn_ = make(L"BUTTON", L"History", BS_OWNERDRAW, IDC_HISTORY, body);
    settings_btn_ = make(L"BUTTON", L"Settings", BS_OWNERDRAW, IDC_SETTINGS, body);

    apply_theme();
    refresh_status();
    refresh_text();

    RECT rc{};
    GetClientRect(hwnd, &rc);
    on_size(rc.right - rc.left, rc.bottom - rc.top);
}

void MainWindow::on_size(int width, int height) {
    const int pad = 18;
    const int status_h = 30;
    const int note_h = 20;
    const int btn_h = 34;
    const int btn_w = 116;
    const int gap = 10;

    int y = pad;
    MoveWindow(static_cast<HWND>(status_label_), pad, y, width - 2 * pad, status_h, TRUE);
    y += status_h + 2;
    MoveWindow(static_cast<HWND>(note_label_), pad, y, width - 2 * pad, note_h, TRUE);
    y += note_h + 10;

    const int text_bottom = height - pad - btn_h - pad;
    MoveWindow(static_cast<HWND>(text_view_), pad, y, width - 2 * pad,
               (text_bottom > y) ? text_bottom - y : 0, TRUE);

    const int by = height - pad - btn_h;
    MoveWindow(static_cast<HWND>(toggle_btn_), pad, by, btn_w, btn_h, TRUE);
    MoveWindow(static_cast<HWND>(copy_btn_), pad + btn_w + gap, by, btn_w, btn_h, TRUE);
    MoveWindow(static_cast<HWND>(history_btn_), pad + 2 * (btn_w + gap), by, btn_w,
               btn_h, TRUE);
    // Settings is right-aligned.
    MoveWindow(static_cast<HWND>(settings_btn_), width - pad - btn_w, by, btn_w,
               btn_h, TRUE);
}

void MainWindow::draw_button(void* dis_v) {
    auto* dis = static_cast<DRAWITEMSTRUCT*>(dis_v);
    HDC dc = dis->hDC;
    RECT r = dis->rcItem;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool primary = (static_cast<int>(dis->CtlID) == IDC_TOGGLE);

    COLORREF fill;
    COLORREF text_color;
    if (primary) {
        fill = pressed ? kAccentPressed : kAccent;
        text_color = RGB(255, 255, 255);
    } else {
        fill = pressed ? col_button_pressed(dark_) : col_button(dark_);
        text_color = col_text(dark_);
    }

    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, fill);
    HGDIOBJ old_brush = SelectObject(dc, brush);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, 8, 8);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);

    wchar_t caption[64] = {};
    GetWindowTextW(dis->hwndItem, caption, 64);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, text_color);
    HGDIOBJ old_font = SelectObject(dc, static_cast<HFONT>(body_font_));
    DrawTextW(dc, caption, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old_font);
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
        case IDC_SETTINGS:
            if (callbacks_.on_open_settings) callbacks_.on_open_settings();
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
    InvalidateRect(static_cast<HWND>(status_label_), nullptr, TRUE);
    InvalidateRect(static_cast<HWND>(toggle_btn_), nullptr, TRUE);
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
    if (bg_brush_ != nullptr) DeleteObject(static_cast<HBRUSH>(bg_brush_));
    if (panel_brush_ != nullptr) DeleteObject(static_cast<HBRUSH>(panel_brush_));
    bg_brush_ = CreateSolidBrush(col_window(dark_));
    panel_brush_ = CreateSolidBrush(col_panel(dark_));

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
                                    const std::string& annotated,
                                    Confidence confidence) {
    model_.set_reconstruction(text, annotated, confidence);
    refresh_text();
}

void MainWindow::set_dark_theme(bool dark) {
    dark_ = dark;
    if (hwnd_ != nullptr) {
        apply_theme();
        InvalidateRect(static_cast<HWND>(hwnd_), nullptr, TRUE);
    }
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
