#include "ui/main_window.hpp"

#include <Windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <charconv>

#include "utils/unicode.hpp"

namespace ir {
namespace {

constexpr wchar_t kClassName[] = L"InputRecorderMainWindow";

constexpr int IDC_NOTE = 1002;
constexpr int IDC_TEXT = 1003;
constexpr int IDC_TOGGLE = 1004;
constexpr int IDC_COPY = 1005;
constexpr int IDC_NAV_LIVE = 1010;
constexpr int IDC_NAV_HISTORY = 1011;
constexpr int IDC_NAV_SETTINGS = 1012;
constexpr int IDC_SEARCH = 2001;
constexpr int IDC_LIST = 2002;
// Settings controls (checkboxes, labels, combo, edit, save)
constexpr int IDC_S_KEYBOARD = 3101;
constexpr int IDC_S_CLICKS = 3102;
constexpr int IDC_S_WHEEL = 3103;
constexpr int IDC_S_MOVEMENT = 3104;
constexpr int IDC_S_WINDOW = 3105;
constexpr int IDC_S_CLIPBOARD = 3106;
constexpr int IDC_S_NOTIFY = 3107;
constexpr int IDC_S_STARTUP = 3108;
constexpr int IDC_S_TRAY = 3109;
constexpr int IDC_S_ONTOP = 3110;
constexpr int IDC_S_LBL_THEME = 3111;
constexpr int IDC_S_THEME = 3112;
constexpr int IDC_S_LBL_RET = 3113;
constexpr int IDC_S_RETENTION = 3114;
constexpr int IDC_S_SAVE = 3115;

constexpr int kSidebarW = 60;
constexpr int kTitleH = 44;
constexpr int kNavSize = 44;
constexpr int kCtlBtnW = 44;
constexpr int kResize = 6;

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
#endif

constexpr COLORREF kMain = RGB(18, 20, 23);
constexpr COLORREF kSidebar = RGB(13, 15, 18);
constexpr COLORREF kPanel = RGB(23, 26, 31);
constexpr COLORREF kText = RGB(212, 216, 223);
constexpr COLORREF kSubtext = RGB(123, 129, 140);
constexpr COLORREF kAccent = RGB(43, 184, 127);
constexpr COLORREF kAccentBright = RGB(55, 211, 154);
constexpr COLORREF kAccentBg = RGB(22, 40, 31);
constexpr COLORREF kBtn = RGB(34, 38, 46);
constexpr COLORREF kBtnHot = RGB(46, 51, 60);
constexpr COLORREF kNavMuted = RGB(118, 124, 134);
constexpr COLORREF kOnAccent = RGB(5, 18, 12);
constexpr COLORREF kCloseHot = RGB(224, 67, 67);
constexpr COLORREF kPaused = RGB(150, 150, 150);

std::wstring to_w(const std::string& utf8) {
    std::u16string u16 = utf8_to_utf16(utf8);
    return std::wstring(reinterpret_cast<const wchar_t*>(u16.data()), u16.size());
}

std::string text_of(HWND h) {
    const int len = GetWindowTextLengthW(h);
    if (len <= 0) return {};
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    const int n = GetWindowTextW(h, buf.data(), static_cast<int>(buf.size()));
    buf.resize(static_cast<size_t>(n < 0 ? 0 : n));
    return utf16_to_utf8(
        std::u16string(reinterpret_cast<const char16_t*>(buf.data()), buf.size()));
}

HFONT make_font(const wchar_t* face, int point_size, int weight) {
    HDC screen = GetDC(nullptr);
    const int height = -MulDiv(point_size, GetDeviceCaps(screen, LOGPIXELSY), 72);
    ReleaseDC(nullptr, screen);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       VARIABLE_PITCH, face);
}

void fill_round(HDC dc, RECT r, COLORREF color, int radius) {
    HBRUSH b = CreateSolidBrush(color);
    HPEN p = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, p);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
    DeleteObject(p);
}

void set_check(HWND parent, int id, bool on) {
    SendMessageW(GetDlgItem(parent, id), BM_SETCHECK,
                 on ? BST_CHECKED : BST_UNCHECKED, 0);
}
bool get_check(HWND parent, int id) {
    return SendMessageW(GetDlgItem(parent, id), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

}  // namespace

MainWindow::~MainWindow() {
    for (void* o : {bg_brush_, sidebar_brush_, panel_brush_})
        if (o != nullptr) DeleteObject(static_cast<HGDIOBJ>(o));
    for (void* f : {body_font_, title_font_, glyph_font_})
        if (f != nullptr) DeleteObject(static_cast<HGDIOBJ>(f));
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
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kClassName, L"Input Recorder", WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, 780, 580, nullptr,
                                nullptr, inst, this);
    hwnd_ = hwnd;
    if (hwnd == nullptr) return false;

    const DWORD round = 2;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof(round));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    return true;
}

void MainWindow::show(int show_command) {
    if (hwnd_ != nullptr) {
        ShowWindow(static_cast<HWND>(hwnd_), show_command);
        UpdateWindow(static_cast<HWND>(hwnd_));
    }
}

int MainWindow::sidebar_w() const { return compact_ ? 0 : kSidebarW; }

int MainWindow::button_at(int x, int y) const {
    RECT rc;
    GetClientRect(static_cast<HWND>(hwnd_), &rc);
    const int w = rc.right;
    if (compact_) {
        // Expand button (top-right of the widget).
        if (x >= w - 40 && x <= w - 8 && y >= 6 && y <= 36) return 4;
        return 0;
    }
    if (y < 0 || y >= kTitleH) return 0;
    if (x >= w - kCtlBtnW) return 2;        // close
    if (x >= w - 2 * kCtlBtnW) return 1;    // minimize
    if (x >= w - 3 * kCtlBtnW) return 3;    // compact toggle
    return 0;
}

long long MainWindow::hit_test(long long lparam) {
    HWND hwnd = static_cast<HWND>(hwnd_);
    POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    RECT wr;
    GetWindowRect(hwnd, &wr);
    const int x = pt.x - wr.left, y = pt.y - wr.top;
    const int w = wr.right - wr.left, h = wr.bottom - wr.top;
    const bool left = x < kResize, right = x >= w - kResize;
    const bool top = y < kResize, bottom = y >= h - kResize;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
    if (compact_) {
        if (button_at(x, y) == 4) return HTCLIENT;  // expand button
        return HTCAPTION;                           // drag the widget
    }
    if (y < kTitleH && x >= sidebar_w()) {
        if (button_at(x, y) != 0) return HTCLIENT;
        return HTCAPTION;
    }
    if (x < sidebar_w()) return HTCAPTION;
    return HTCLIENT;
}

long long MainWindow::handle_message(void* hwnd_v, unsigned msg,
                                     unsigned long long wparam, long long lparam) {
    HWND hwnd = static_cast<HWND>(hwnd_v);
    hwnd_ = hwnd;
    switch (msg) {
        case WM_CREATE:
            on_create();
            return 0;
        case WM_NCCALCSIZE:
            if (wparam != 0) {
                auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                if (IsZoomed(hwnd)) {
                    const int fx = GetSystemMetrics(SM_CXFRAME) +
                                   GetSystemMetrics(SM_CXPADDEDBORDER);
                    const int fy = GetSystemMetrics(SM_CYFRAME) +
                                   GetSystemMetrics(SM_CXPADDEDBORDER);
                    p->rgrc[0].left += fx;
                    p->rgrc[0].right -= fx;
                    p->rgrc[0].top += fy;
                    p->rgrc[0].bottom -= fy;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST:
            return hit_test(lparam);
        case WM_PAINT:
            paint_chrome();
            return 0;
        case WM_SIZE:
            on_size(LOWORD(lparam), HIWORD(lparam));
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            const int b = button_at(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (b != hot_btn_) {
                hot_btn_ = b;
                RECT tb;
                GetClientRect(hwnd, &tb);
                tb.bottom = kTitleH;
                InvalidateRect(hwnd, &tb, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hot_btn_ != 0) {
                hot_btn_ = 0;
                RECT tb;
                GetClientRect(hwnd, &tb);
                tb.bottom = kTitleH;
                InvalidateRect(hwnd, &tb, FALSE);
            }
            return 0;
        case WM_LBUTTONDOWN:
            pressed_btn_ = button_at(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            return 0;
        case WM_LBUTTONUP: {
            const int b = button_at(GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
            if (b != 0 && b == pressed_btn_) title_button_action(b);
            pressed_btn_ = 0;
            return 0;
        }
        case WM_COMMAND:
            on_command(LOWORD(wparam), HIWORD(wparam));
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
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc,
                         reinterpret_cast<HWND>(lparam) == note_label_ ? kSubtext : kText);
            SetBkColor(dc, kMain);
            return reinterpret_cast<long long>(bg_brush_);
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, kText);
            SetBkColor(dc, kPanel);
            return reinterpret_cast<long long>(panel_brush_);
        }
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);  // hide to tray; exit via the tray menu
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

void MainWindow::title_button_action(int button) {
    HWND hwnd = static_cast<HWND>(hwnd_);
    if (button == 1) ShowWindow(hwnd, SW_MINIMIZE);
    else if (button == 2) SendMessageW(hwnd, WM_CLOSE, 0, 0);
    else if (button == 3 || button == 4) toggle_compact();
}

void MainWindow::toggle_compact() {
    HWND hwnd = static_cast<HWND>(hwnd_);
    compact_ = !compact_;
    if (compact_) {
        view_ = kViewLive;
        // A tiny always-on-top widget docked to the bottom-left of the work area
        // (left edge, just above the taskbar).
        RECT wa{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        const int cw = 230, ch = 82;
        SetWindowPos(hwnd, HWND_TOPMOST, wa.left + 12, wa.bottom - ch - 12, cw, ch,
                     SWP_SHOWWINDOW);
    } else {
        SetWindowPos(hwnd, HWND_NOTOPMOST, 120, 90, 780, 580, SWP_SHOWWINDOW);
    }
    activate_view(view_);
}

void MainWindow::paint_chrome() {
    HWND hwnd = static_cast<HWND>(hwnd_);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right, h = rc.bottom;

    if (compact_) {
        RECT full{0, 0, w, h};
        FillRect(dc, &full, static_cast<HBRUSH>(bg_brush_));
        SetBkMode(dc, TRANSPARENT);
        const bool rec = is_recording(model_.status());

        const int srow = 20;  // status row vertical center
        HBRUSH dotb = CreateSolidBrush(rec ? kAccentBright : kPaused);
        HGDIOBJ odb = SelectObject(dc, dotb);
        HGDIOBJ odp = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, 14, srow - 5, 24, srow + 5);
        SelectObject(dc, odb);
        SelectObject(dc, odp);
        DeleteObject(dotb);

        SetTextColor(dc, rec ? kAccentBright : kPaused);
        HGDIOBJ of = SelectObject(dc, static_cast<HFONT>(title_font_));
        RECT sr{30, 0, w - 42, 40};  // tall rect so the text is never clipped
        DrawTextW(dc, rec ? L"Recording" : L"Paused", -1, &sr,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);

        // Expand button (top-right).
        RECT er{w - 36, srow - 11, w - 10, srow + 11};
        if (hot_btn_ == 4) fill_round(dc, er, kBtn, 6);
        HPEN pen = CreatePen(PS_SOLID, 1, kText);
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        const int ex = (er.left + er.right) / 2;
        Rectangle(dc, ex - 6, srow - 5, ex + 6, srow + 6);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(pen);

        HPEN dpen = CreatePen(PS_SOLID, 1, RGB(38, 42, 50));
        HGDIOBJ dop = SelectObject(dc, dpen);
        MoveToEx(dc, 14, 40, nullptr);
        LineTo(dc, w - 14, 40);
        SelectObject(dc, dop);
        DeleteObject(dpen);

        // Last input value (no label) = last non-empty line of the annotated view.
        std::string disp = model_.display_text();
        std::string last;
        if (disp.rfind("No text", 0) != 0) {
            const size_t end = disp.find_last_not_of("\r\n");
            if (end != std::string::npos) {
                size_t start = disp.find_last_of('\n', end);
                start = (start == std::string::npos) ? 0 : start + 1;
                last = disp.substr(start, end - start + 1);
                if (!last.empty() && last.back() == '\r') last.pop_back();
            }
        }
        if (last.empty()) last = "\xE2\x80\x94";  // em dash
        SetTextColor(dc, kText);
        of = SelectObject(dc, static_cast<HFONT>(title_font_));
        const std::wstring lw = to_w(last);
        RECT lv{14, 44, w - 14, h - 6};
        DrawTextW(dc, lw.c_str(), -1, &lv,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, of);

        EndPaint(hwnd, &ps);
        return;
    }

    const int sw = sidebar_w();
    RECT tbar{sw, 0, w, kTitleH};
    FillRect(dc, &tbar, static_cast<HBRUSH>(bg_brush_));
    if (!compact_) {
        RECT rail{0, 0, kSidebarW, h};
        FillRect(dc, &rail, static_cast<HBRUSH>(sidebar_brush_));
    }

    SetBkMode(dc, TRANSPARENT);
    // App name (left).
    SetTextColor(dc, kText);
    HGDIOBJ of = SelectObject(dc, static_cast<HFONT>(title_font_));
    RECT lr{sw + 16, 0, sw + 240, kTitleH};
    DrawTextW(dc, L"Input Recorder", -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);

    // Recording status (right, before the window buttons): a colored dot + text.
    const bool rec = is_recording(model_.status());
    const wchar_t* st = rec ? L"RECORDING" : L"PAUSED";
    of = SelectObject(dc, static_cast<HFONT>(body_font_));
    SIZE ts{};
    GetTextExtentPoint32W(dc, st, lstrlenW(st), &ts);
    const int cy = kTitleH / 2;
    const int status_right = w - 3 * kCtlBtnW - 14;
    const int tx = status_right - ts.cx;
    HBRUSH dotb = CreateSolidBrush(rec ? kAccentBright : kPaused);
    HGDIOBJ odb = SelectObject(dc, dotb);
    HGDIOBJ odp = SelectObject(dc, GetStockObject(NULL_PEN));
    const int dot_x = tx - 15;
    Ellipse(dc, dot_x, cy - 4, dot_x + 8, cy + 4);
    SelectObject(dc, odb);
    SelectObject(dc, odp);
    DeleteObject(dotb);
    SetTextColor(dc, rec ? kAccentBright : kPaused);
    RECT sr{tx, 0, status_right, kTitleH};
    DrawTextW(dc, st, -1, &sr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);

    // Window buttons: compact toggle, minimize, close.
    RECT cmpr{w - 3 * kCtlBtnW, 0, w - 2 * kCtlBtnW, kTitleH};
    RECT minr{w - 2 * kCtlBtnW, 0, w - kCtlBtnW, kTitleH};
    RECT clsr{w - kCtlBtnW, 0, w, kTitleH};
    if (hot_btn_ == 3) {
        HBRUSH hb = CreateSolidBrush(kBtn);
        FillRect(dc, &cmpr, hb);
        DeleteObject(hb);
    }
    if (hot_btn_ == 1) {
        HBRUSH hb = CreateSolidBrush(kBtn);
        FillRect(dc, &minr, hb);
        DeleteObject(hb);
    }
    if (hot_btn_ == 2) {
        HBRUSH hb = CreateSolidBrush(kCloseHot);
        FillRect(dc, &clsr, hb);
        DeleteObject(hb);
    }
    {
        HPEN pen = CreatePen(PS_SOLID, 1, kText);
        HGDIOBJ op = SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(HOLLOW_BRUSH));
        // compact toggle: a small window rectangle.
        const int ccx = (cmpr.left + cmpr.right) / 2;
        Rectangle(dc, ccx - 6, cy - 4, ccx + 6, cy + 5);
        SelectObject(dc, ob);
        // minimize: a line.
        const int mcx = (minr.left + minr.right) / 2;
        MoveToEx(dc, mcx - 6, cy, nullptr);
        LineTo(dc, mcx + 6, cy);
        // close: an X.
        const COLORREF xcol = (hot_btn_ == 2) ? RGB(255, 255, 255) : kText;
        HPEN pen2 = CreatePen(PS_SOLID, 1, xcol);
        SelectObject(dc, pen2);
        const int xcx = (clsr.left + clsr.right) / 2;
        MoveToEx(dc, xcx - 5, cy - 5, nullptr);
        LineTo(dc, xcx + 6, cy + 6);
        MoveToEx(dc, xcx + 5, cy - 5, nullptr);
        LineTo(dc, xcx - 6, cy + 6);
        SelectObject(dc, op);
        DeleteObject(pen);
        DeleteObject(pen2);
    }
    EndPaint(hwnd, &ps);
}

void MainWindow::build_history_controls(void* inst_v) {
    HWND hwnd = static_cast<HWND>(hwnd_);
    HINSTANCE inst = static_cast<HINSTANCE>(inst_v);
    HFONT body = static_cast<HFONT>(body_font_);
    search_edit_ = CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SEARCH)), inst, nullptr);
    list_box_ = CreateWindowExW(
        0, L"LISTBOX", L"",
        WS_CHILD | WS_VSCROLL | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS, 0, 0, 0, 0,
        hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)), inst, nullptr);
    SendMessageW(static_cast<HWND>(search_edit_), WM_SETFONT,
                 reinterpret_cast<WPARAM>(body), TRUE);
    SendMessageW(static_cast<HWND>(list_box_), WM_SETFONT,
                 reinterpret_cast<WPARAM>(body), TRUE);
    SendMessageW(static_cast<HWND>(search_edit_), EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Search recorded text…"));
    SetWindowTheme(static_cast<HWND>(list_box_), L"DarkMode_Explorer", nullptr);
}

void MainWindow::build_settings_controls(void* inst_v) {
    HWND hwnd = static_cast<HWND>(hwnd_);
    HINSTANCE inst = static_cast<HINSTANCE>(inst_v);
    HFONT body = static_cast<HFONT>(body_font_);

    auto add = [&](const wchar_t* cls, const wchar_t* text, DWORD style,
                   int id) -> HWND {
        HWND h = CreateWindowExW(
            0, cls, text, WS_CHILD | style, 0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(body), TRUE);
        settings_ctrls_.push_back(h);
        return h;
    };

    const DWORD cb = BS_AUTOCHECKBOX;
    add(L"BUTTON", L"Record keyboard", cb, IDC_S_KEYBOARD);
    add(L"BUTTON", L"Record mouse clicks", cb, IDC_S_CLICKS);
    add(L"BUTTON", L"Record mouse wheel", cb, IDC_S_WHEEL);
    add(L"BUTTON", L"Record mouse movement", cb, IDC_S_MOVEMENT);
    add(L"BUTTON", L"Track active window", cb, IDC_S_WINDOW);
    add(L"BUTTON", L"Monitor clipboard", cb, IDC_S_CLIPBOARD);
    add(L"BUTTON", L"Show notifications", cb, IDC_S_NOTIFY);
    add(L"BUTTON", L"Start with Windows", cb, IDC_S_STARTUP);
    add(L"BUTTON", L"Minimize to tray", cb, IDC_S_TRAY);
    add(L"BUTTON", L"Always on top", cb, IDC_S_ONTOP);

    add(L"STATIC", L"Theme", SS_LEFT, IDC_S_LBL_THEME);
    HWND theme = add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, IDC_S_THEME);
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"System"));
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Light"));
    SendMessageW(theme, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Dark"));

    add(L"STATIC", L"Retention (days):", SS_LEFT, IDC_S_LBL_RET);
    add(L"EDIT", L"", WS_BORDER | ES_NUMBER, IDC_S_RETENTION);
    add(L"BUTTON", L"Save", BS_OWNERDRAW, IDC_S_SAVE);

    for (void* c : settings_ctrls_)
        SetWindowTheme(static_cast<HWND>(c), L"DarkMode_Explorer", nullptr);
}

void MainWindow::on_create() {
    HWND hwnd = static_cast<HWND>(hwnd_);
    HINSTANCE inst =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));

    body_font_ = make_font(L"Segoe UI", 10, FW_NORMAL);
    title_font_ = make_font(L"Segoe UI", 12, FW_SEMIBOLD);
    glyph_font_ = make_font(L"Segoe UI Symbol", 15, FW_NORMAL);

    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) -> HWND {
        HWND x = CreateWindowExW(
            0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        SendMessageW(x, WM_SETFONT,
                     reinterpret_cast<WPARAM>(static_cast<HFONT>(body_font_)), TRUE);
        return x;
    };

    note_label_ = make(L"STATIC", L"", SS_LEFTNOWORDWRAP, IDC_NOTE);
    text_view_ = make(L"EDIT", L"",
                      ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                      IDC_TEXT);
    SetWindowTheme(static_cast<HWND>(text_view_), L"DarkMode_Explorer", nullptr);
    toggle_btn_ = make(L"BUTTON", L"Pause", BS_OWNERDRAW, IDC_TOGGLE);
    copy_btn_ = make(L"BUTTON", L"Copy All", BS_OWNERDRAW, IDC_COPY);

    nav_live_ = make(L"BUTTON", L"\x25CF", BS_OWNERDRAW, IDC_NAV_LIVE);
    nav_history_ = make(L"BUTTON", L"\x25A4", BS_OWNERDRAW, IDC_NAV_HISTORY);
    nav_settings_ = make(L"BUTTON", L"\x2699", BS_OWNERDRAW, IDC_NAV_SETTINGS);

    build_history_controls(inst);
    build_settings_controls(inst);

    apply_theme();
    refresh_status();
    refresh_text();

    RECT rc{};
    GetClientRect(hwnd, &rc);
    on_size(rc.right - rc.left, rc.bottom - rc.top);
    activate_view(kViewLive);
}

void MainWindow::layout_content(int width, int height) {
    HWND hwnd = static_cast<HWND>(hwnd_);
    if (compact_) return;  // the compact widget has no child controls to lay out

    const int navx = (kSidebarW - kNavSize) / 2;
    MoveWindow(static_cast<HWND>(nav_live_), navx, kTitleH + 8, kNavSize, kNavSize, TRUE);
    MoveWindow(static_cast<HWND>(nav_history_), navx, kTitleH + 8 + kNavSize + 8,
               kNavSize, kNavSize, TRUE);
    MoveWindow(static_cast<HWND>(nav_settings_), navx, kTitleH + 8 + 2 * (kNavSize + 8),
               kNavSize, kNavSize, TRUE);

    const int left = kSidebarW + 18, rpad = 18;
    const int cw = width - left - rpad;
    const int top = kTitleH + 12;
    const int btn_h = 34, btn_w = 116, gap = 10;

    // Live view
    MoveWindow(static_cast<HWND>(note_label_), left, top, cw, 20, TRUE);
    const int ty = top + 26;
    const int text_bottom = height - 18 - btn_h - 12;
    MoveWindow(static_cast<HWND>(text_view_), left, ty, cw,
               (text_bottom > ty) ? text_bottom - ty : 0, TRUE);
    const int by = height - 18 - btn_h;
    MoveWindow(static_cast<HWND>(toggle_btn_), left, by, btn_w, btn_h, TRUE);
    MoveWindow(static_cast<HWND>(copy_btn_), left + btn_w + gap, by, btn_w, btn_h, TRUE);

    // History view
    MoveWindow(static_cast<HWND>(search_edit_), left, top, cw, 28, TRUE);
    const int ly = top + 36;
    MoveWindow(static_cast<HWND>(list_box_), left, ly, cw,
               (height - ly - 18 > 0) ? height - ly - 18 : 0, TRUE);

    // Settings view
    int y = top;
    const int step = 26;
    for (int id = IDC_S_KEYBOARD; id <= IDC_S_ONTOP; ++id) {
        MoveWindow(GetDlgItem(hwnd, id), left, y, cw, 22, TRUE);
        y += step;
    }
    y += 8;
    MoveWindow(GetDlgItem(hwnd, IDC_S_LBL_THEME), left, y + 4, 60, 20, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_S_THEME), left + 70, y, 160, 160, TRUE);
    y += 34;
    MoveWindow(GetDlgItem(hwnd, IDC_S_LBL_RET), left, y + 4, 120, 20, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_S_RETENTION), left + 126, y, 70, 24, TRUE);
    y += 42;
    MoveWindow(GetDlgItem(hwnd, IDC_S_SAVE), left, y, 100, btn_h, TRUE);
}

void MainWindow::on_size(int width, int height) { layout_content(width, height); }

void MainWindow::activate_view(int view) {
    view_ = view;
    HWND hwnd = static_cast<HWND>(hwnd_);
    auto show = [](void* c, bool s) {
        if (c) ShowWindow(static_cast<HWND>(c), s ? SW_SHOW : SW_HIDE);
    };

    if (compact_) {
        // Compact widget is fully custom-painted — hide every child control.
        show(text_view_, false);
        show(note_label_, false);
        show(toggle_btn_, false);
        show(copy_btn_, false);
        show(search_edit_, false);
        show(list_box_, false);
        for (void* c : settings_ctrls_) show(c, false);
        show(nav_live_, false);
        show(nav_history_, false);
        show(nav_settings_, false);
        InvalidateRect(hwnd, nullptr, TRUE);
        return;
    }

    show(nav_live_, true);
    show(nav_history_, true);
    show(nav_settings_, true);
    const bool live = view == kViewLive, hist = view == kViewHistory,
               set = view == kViewSettings;
    show(note_label_, live);
    show(text_view_, live);
    show(toggle_btn_, live);
    show(copy_btn_, live);
    show(search_edit_, hist);
    show(list_box_, hist);
    for (void* c : settings_ctrls_) show(c, set);
    if (hist) refresh_history();
    if (set) load_settings_controls();
    InvalidateRect(hwnd, nullptr, TRUE);
}

void MainWindow::refresh_history() {
    if (list_box_ == nullptr || !callbacks_.history_source) return;
    const std::string query = text_of(static_cast<HWND>(search_edit_));
    const std::vector<std::string> rows = callbacks_.history_source(query);
    HWND list = static_cast<HWND>(list_box_);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const std::string& row : rows) {
        const std::wstring wr = to_w(row);
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wr.c_str()));
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

void MainWindow::load_settings_controls() {
    if (!callbacks_.get_config) return;
    HWND hwnd = static_cast<HWND>(hwnd_);
    const Configuration c = callbacks_.get_config();
    set_check(hwnd, IDC_S_KEYBOARD, c.record_keyboard);
    set_check(hwnd, IDC_S_CLICKS, c.record_mouse_clicks);
    set_check(hwnd, IDC_S_WHEEL, c.record_mouse_wheel);
    set_check(hwnd, IDC_S_MOVEMENT, c.record_mouse_movement);
    set_check(hwnd, IDC_S_WINDOW, c.track_active_window);
    set_check(hwnd, IDC_S_CLIPBOARD, c.monitor_clipboard);
    set_check(hwnd, IDC_S_NOTIFY, c.show_notifications);
    set_check(hwnd, IDC_S_STARTUP, c.start_with_windows);
    set_check(hwnd, IDC_S_TRAY, c.minimize_to_tray);
    set_check(hwnd, IDC_S_ONTOP, c.always_on_top);
    int theme_index = c.theme == Theme::Light ? 1 : c.theme == Theme::Dark ? 2 : 0;
    SendMessageW(GetDlgItem(hwnd, IDC_S_THEME), CB_SETCURSEL, theme_index, 0);
    SetWindowTextW(GetDlgItem(hwnd, IDC_S_RETENTION),
                   std::to_wstring(c.retention_days).c_str());
}

Configuration MainWindow::read_settings_controls() const {
    HWND hwnd = static_cast<HWND>(hwnd_);
    Configuration c = callbacks_.get_config ? callbacks_.get_config() : Configuration{};
    c.record_keyboard = get_check(hwnd, IDC_S_KEYBOARD);
    c.record_mouse_clicks = get_check(hwnd, IDC_S_CLICKS);
    c.record_mouse_wheel = get_check(hwnd, IDC_S_WHEEL);
    c.record_mouse_movement = get_check(hwnd, IDC_S_MOVEMENT);
    c.track_active_window = get_check(hwnd, IDC_S_WINDOW);
    c.monitor_clipboard = get_check(hwnd, IDC_S_CLIPBOARD);
    c.show_notifications = get_check(hwnd, IDC_S_NOTIFY);
    c.start_with_windows = get_check(hwnd, IDC_S_STARTUP);
    c.minimize_to_tray = get_check(hwnd, IDC_S_TRAY);
    c.always_on_top = get_check(hwnd, IDC_S_ONTOP);
    switch (SendMessageW(GetDlgItem(hwnd, IDC_S_THEME), CB_GETCURSEL, 0, 0)) {
        case 1: c.theme = Theme::Light; break;
        case 2: c.theme = Theme::Dark; break;
        default: c.theme = Theme::System; break;
    }
    const std::string ret = text_of(GetDlgItem(hwnd, IDC_S_RETENTION));
    int days = c.retention_days;
    std::from_chars(ret.data(), ret.data() + ret.size(), days);
    c.retention_days = days;
    validate(c);
    return c;
}

void MainWindow::draw_button(void* dis_v) {
    auto* dis = static_cast<DRAWITEMSTRUCT*>(dis_v);
    HDC dc = dis->hDC;
    RECT r = dis->rcItem;
    const int id = static_cast<int>(dis->CtlID);
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool is_nav =
        (id == IDC_NAV_LIVE || id == IDC_NAV_HISTORY || id == IDC_NAV_SETTINGS);

    wchar_t caption[64] = {};
    GetWindowTextW(dis->hwndItem, caption, 64);
    SetBkMode(dc, TRANSPARENT);

    if (is_nav) {
        const bool active = (id == IDC_NAV_LIVE && view_ == kViewLive) ||
                            (id == IDC_NAV_HISTORY && view_ == kViewHistory) ||
                            (id == IDC_NAV_SETTINGS && view_ == kViewSettings);
        HBRUSH railb = CreateSolidBrush(kSidebar);
        FillRect(dc, &r, railb);
        DeleteObject(railb);
        if (active) fill_round(dc, r, kAccentBg, 10);
        else if (pressed) fill_round(dc, r, kBtn, 10);
        SetTextColor(dc, active ? kAccentBright : kNavMuted);
        HGDIOBJ of = SelectObject(dc, static_cast<HFONT>(glyph_font_));
        DrawTextW(dc, caption, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
        return;
    }

    const bool primary = (id == IDC_TOGGLE || id == IDC_S_SAVE);
    COLORREF fill = primary ? kAccent : kBtn;
    if (pressed) fill = primary ? RGB(36, 156, 108) : kBtnHot;
    const COLORREF txt = primary ? kOnAccent : kText;
    HBRUSH bgb = CreateSolidBrush(kMain);
    FillRect(dc, &r, bgb);
    DeleteObject(bgb);
    fill_round(dc, r, fill, 8);
    SetTextColor(dc, txt);
    HGDIOBJ of = SelectObject(dc, static_cast<HFONT>(body_font_));
    DrawTextW(dc, caption, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
}

void MainWindow::on_command(int control_id, int notify) {
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
        case IDC_NAV_LIVE:
            activate_view(kViewLive);
            break;
        case IDC_NAV_HISTORY:
            activate_view(kViewHistory);
            break;
        case IDC_NAV_SETTINGS:
            activate_view(kViewSettings);
            break;
        case IDC_SEARCH:
            if (notify == EN_CHANGE) refresh_history();
            break;
        case IDC_S_SAVE:
            if (callbacks_.on_save_settings)
                callbacks_.on_save_settings(read_settings_controls());
            activate_view(kViewLive);
            break;
        default:
            break;
    }
}

void MainWindow::refresh_status() {
    if (toggle_btn_ == nullptr) return;
    SetWindowTextW(static_cast<HWND>(toggle_btn_),
                   is_recording(model_.status()) ? L"Pause" : L"Resume");
    InvalidateRect(static_cast<HWND>(toggle_btn_), nullptr, TRUE);
    RECT tb;
    GetClientRect(static_cast<HWND>(hwnd_), &tb);
    tb.bottom = kTitleH;
    InvalidateRect(static_cast<HWND>(hwnd_), &tb, FALSE);
}

void MainWindow::refresh_text() {
    if (text_view_ == nullptr) return;
    HWND edit = static_cast<HWND>(text_view_);
    SetWindowTextW(static_cast<HWND>(note_label_),
                   to_w(model_.confidence_note()).c_str());

    const std::string display = model_.display_text();
    if (display == last_display_) return;

    if (compact_) {
        last_display_ = display;
        InvalidateRect(static_cast<HWND>(hwnd_), nullptr, FALSE);  // repaint widget
        return;
    }

    const int first_before =
        static_cast<int>(SendMessageW(edit, EM_GETFIRSTVISIBLELINE, 0, 0));
    const int line_count =
        static_cast<int>(SendMessageW(edit, EM_GETLINECOUNT, 0, 0));
    RECT er;
    GetClientRect(edit, &er);
    TEXTMETRICW tm{};
    HDC edc = GetDC(edit);
    HGDIOBJ of = SelectObject(edc, static_cast<HFONT>(body_font_));
    GetTextMetricsW(edc, &tm);
    SelectObject(edc, of);
    ReleaseDC(edit, edc);
    const int visible_lines = tm.tmHeight > 0 ? er.bottom / tm.tmHeight : 1;
    const bool at_bottom = (first_before + visible_lines) >= (line_count - 1);

    last_display_ = display;
    SetWindowTextW(edit, to_w(display).c_str());
    if (at_bottom) {
        const int lines = static_cast<int>(SendMessageW(edit, EM_GETLINECOUNT, 0, 0));
        SendMessageW(edit, EM_LINESCROLL, 0, lines);
    } else {
        SendMessageW(edit, EM_LINESCROLL, 0, first_before);
    }
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
    if (bg_brush_ != nullptr) DeleteObject(static_cast<HGDIOBJ>(bg_brush_));
    if (sidebar_brush_ != nullptr) DeleteObject(static_cast<HGDIOBJ>(sidebar_brush_));
    if (panel_brush_ != nullptr) DeleteObject(static_cast<HGDIOBJ>(panel_brush_));
    bg_brush_ = CreateSolidBrush(kMain);
    sidebar_brush_ = CreateSolidBrush(kSidebar);
    panel_brush_ = CreateSolidBrush(kPanel);
    const BOOL dark = TRUE;
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

void MainWindow::set_dark_theme(bool /*dark*/) {}

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
