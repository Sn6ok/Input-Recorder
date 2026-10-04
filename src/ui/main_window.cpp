#include "ui/main_window.hpp"

#include <Windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>

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

// "Studio" dark palette (teal/green accent).
constexpr COLORREF kMain = RGB(18, 20, 23);         // #121417 main area
constexpr COLORREF kSidebar = RGB(13, 15, 18);      // #0d0f12 nav rail
constexpr COLORREF kPanel = RGB(23, 26, 31);        // #171a1f text panel
constexpr COLORREF kText = RGB(212, 216, 223);      // #d4d8df
constexpr COLORREF kSubtext = RGB(123, 129, 140);   // #7b818c
constexpr COLORREF kAccent = RGB(43, 184, 127);     // #2bb87f
constexpr COLORREF kAccentBright = RGB(55, 211, 154); // #37d39a
constexpr COLORREF kAccentBg = RGB(22, 40, 31);     // #16281f
constexpr COLORREF kBtn = RGB(34, 38, 46);          // neutral button
constexpr COLORREF kBtnHot = RGB(46, 51, 60);
constexpr COLORREF kNavMuted = RGB(118, 124, 134);
constexpr COLORREF kOnAccent = RGB(5, 18, 12);      // dark text on green
constexpr COLORREF kCloseHot = RGB(224, 67, 67);
constexpr COLORREF kPaused = RGB(150, 150, 150);

std::wstring to_w(const std::string& utf8) {
    std::u16string u16 = utf8_to_utf16(utf8);
    return std::wstring(reinterpret_cast<const wchar_t*>(u16.data()), u16.size());
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
                                CW_USEDEFAULT, CW_USEDEFAULT, 760, 560, nullptr,
                                nullptr, inst, this);
    hwnd_ = hwnd;
    if (hwnd == nullptr) return false;

    const DWORD round = 2;  // DWMWCP_ROUND
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

int MainWindow::button_at(int x, int y) const {
    if (y < 0 || y >= kTitleH) return 0;
    RECT rc;
    GetClientRect(static_cast<HWND>(hwnd_), &rc);
    const int w = rc.right;
    if (x >= w - kCtlBtnW) return 2;       // close
    if (x >= w - 2 * kCtlBtnW) return 1;   // minimize
    return 0;
}

long long MainWindow::hit_test(long long lparam) {
    HWND hwnd = static_cast<HWND>(hwnd_);
    POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    RECT wr;
    GetWindowRect(hwnd, &wr);
    const int x = pt.x - wr.left;
    const int y = pt.y - wr.top;
    const int w = wr.right - wr.left;
    const int h = wr.bottom - wr.top;

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
    if (y < kTitleH && x >= kSidebarW) {
        if (button_at(x, y) != 0) return HTCLIENT;
        return HTCAPTION;
    }
    if (x < kSidebarW) return HTCAPTION;  // drag from the rail (nav buttons are children)
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
                RECT tb{0, 0, 0, kTitleH};
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
            SetTextColor(dc, kSubtext);
            SetBkColor(dc, kMain);
            return reinterpret_cast<long long>(bg_brush_);
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wparam);
            SetTextColor(dc, kText);
            SetBkColor(dc, kPanel);
            return reinterpret_cast<long long>(panel_brush_);
        }
        case WM_CLOSE:
            // The recorder keeps running in the tray; closing just hides the
            // window. Full exit is via the tray menu (spec: visible, user
            // controlled). close_to_tray_ is kept for compatibility but the
            // window always hides rather than quitting.
            ShowWindow(hwnd, SW_HIDE);
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
    if (button == 1) {
        ShowWindow(hwnd, SW_MINIMIZE);
    } else if (button == 2) {
        SendMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

void MainWindow::paint_chrome() {
    HWND hwnd = static_cast<HWND>(hwnd_);
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);

    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right;
    const int h = rc.bottom;

    // Title-bar background (clears any previous pill/text so a shorter label
    // like REC does not leave leftovers from a wider one like PAUSED).
    RECT tbar{kSidebarW, 0, w, kTitleH};
    FillRect(dc, &tbar, static_cast<HBRUSH>(bg_brush_));

    // Navigation rail background.
    RECT rail{0, 0, kSidebarW, h};
    FillRect(dc, &rail, static_cast<HBRUSH>(sidebar_brush_));

    SetBkMode(dc, TRANSPARENT);

    // "Live" label.
    SetTextColor(dc, kText);
    HGDIOBJ of = SelectObject(dc, static_cast<HFONT>(title_font_));
    RECT lr{kSidebarW + 16, 0, kSidebarW + 80, kTitleH};
    DrawTextW(dc, L"Live", -1, &lr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);

    // Recording pill.
    const bool rec = is_recording(model_.status());
    const wchar_t* pill_text = rec ? L"REC" : L"PAUSED";
    of = SelectObject(dc, static_cast<HFONT>(body_font_));
    SIZE ts{};
    GetTextExtentPoint32W(dc, pill_text, lstrlenW(pill_text), &ts);
    SelectObject(dc, of);
    const int py = (kTitleH - 22) / 2;
    const int pill_x = kSidebarW + 66;
    const int text_off = 12 + 8 + 7;  // left pad + dot + gap
    const int pill_w = text_off + ts.cx + 12;
    RECT pill{pill_x, py, pill_x + pill_w, py + 22};
    fill_round(dc, pill, rec ? kAccentBg : kBtn, 11);
    HBRUSH dotb = CreateSolidBrush(rec ? kAccentBright : kPaused);
    HGDIOBJ odb = SelectObject(dc, dotb);
    HGDIOBJ odp = SelectObject(dc, GetStockObject(NULL_PEN));
    const int dcy = py + 11;
    Ellipse(dc, pill.left + 12, dcy - 4, pill.left + 20, dcy + 4);
    SelectObject(dc, odb);
    SelectObject(dc, odp);
    DeleteObject(dotb);
    SetTextColor(dc, rec ? kAccentBright : kPaused);
    of = SelectObject(dc, static_cast<HFONT>(body_font_));
    RECT ptr{pill.left + text_off, pill.top, pill.right, pill.bottom};
    DrawTextW(dc, pill_text, -1, &ptr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);

    // Window controls (minimize / close).
    RECT minr{w - 2 * kCtlBtnW, 0, w - kCtlBtnW, kTitleH};
    RECT clsr{w - kCtlBtnW, 0, w, kTitleH};
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
        int cx = (minr.left + minr.right) / 2, cy = kTitleH / 2;
        MoveToEx(dc, cx - 6, cy, nullptr);
        LineTo(dc, cx + 6, cy);
        const COLORREF xcol = (hot_btn_ == 2) ? RGB(255, 255, 255) : kText;
        HPEN pen2 = CreatePen(PS_SOLID, 1, xcol);
        SelectObject(dc, pen2);
        cx = (clsr.left + clsr.right) / 2;
        MoveToEx(dc, cx - 5, cy - 5, nullptr);
        LineTo(dc, cx + 6, cy + 6);
        MoveToEx(dc, cx + 5, cy - 5, nullptr);
        LineTo(dc, cx - 6, cy + 6);
        SelectObject(dc, op);
        DeleteObject(pen);
        DeleteObject(pen2);
    }

    EndPaint(hwnd, &ps);
}

void MainWindow::on_create() {
    HWND hwnd = static_cast<HWND>(hwnd_);
    HINSTANCE inst =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));

    body_font_ = make_font(L"Segoe UI", 10, FW_NORMAL);
    title_font_ = make_font(L"Segoe UI", 12, FW_SEMIBOLD);
    glyph_font_ = make_font(L"Segoe UI Symbol", 15, FW_NORMAL);

    auto make = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id,
                    HFONT font) -> HWND {
        HWND x = CreateWindowExW(
            0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), inst, nullptr);
        SendMessageW(x, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return x;
    };

    HFONT body = static_cast<HFONT>(body_font_);

    note_label_ = make(L"STATIC", L"", SS_LEFTNOWORDWRAP, IDC_NOTE, body);
    text_view_ = make(L"EDIT", L"",
                      ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                      IDC_TEXT, body);
    // Dark scrollbar to match the theme (replaces the bright system scrollbar).
    SetWindowTheme(static_cast<HWND>(text_view_), L"DarkMode_Explorer", nullptr);
    toggle_btn_ = make(L"BUTTON", L"Pause", BS_OWNERDRAW, IDC_TOGGLE, body);
    copy_btn_ = make(L"BUTTON", L"Copy All", BS_OWNERDRAW, IDC_COPY, body);

    nav_live_ = make(L"BUTTON", L"\x25CF", BS_OWNERDRAW, IDC_NAV_LIVE, body);
    nav_history_ = make(L"BUTTON", L"\x25A4", BS_OWNERDRAW, IDC_NAV_HISTORY, body);
    nav_settings_ = make(L"BUTTON", L"\x2699", BS_OWNERDRAW, IDC_NAV_SETTINGS, body);

    apply_theme();
    refresh_status();
    refresh_text();

    RECT rc{};
    GetClientRect(hwnd, &rc);
    on_size(rc.right - rc.left, rc.bottom - rc.top);
}

void MainWindow::on_size(int width, int height) {
    const int navx = (kSidebarW - kNavSize) / 2;
    MoveWindow(static_cast<HWND>(nav_live_), navx, kTitleH + 8, kNavSize, kNavSize, TRUE);
    MoveWindow(static_cast<HWND>(nav_history_), navx, kTitleH + 8 + kNavSize + 8,
               kNavSize, kNavSize, TRUE);
    MoveWindow(static_cast<HWND>(nav_settings_), navx, kTitleH + 8 + 2 * (kNavSize + 8),
               kNavSize, kNavSize, TRUE);

    const int left = kSidebarW + 18;
    const int right_pad = 18;
    const int content_w = width - left - right_pad;
    const int btn_h = 34, btn_w = 116, gap = 10;

    int y = kTitleH + 10;
    MoveWindow(static_cast<HWND>(note_label_), left, y, content_w, 20, TRUE);
    y += 26;

    const int text_bottom = height - 18 - btn_h - 12;
    MoveWindow(static_cast<HWND>(text_view_), left, y, content_w,
               (text_bottom > y) ? text_bottom - y : 0, TRUE);

    const int by = height - 18 - btn_h;
    MoveWindow(static_cast<HWND>(toggle_btn_), left, by, btn_w, btn_h, TRUE);
    MoveWindow(static_cast<HWND>(copy_btn_), left + btn_w + gap, by, btn_w, btn_h, TRUE);
}

void MainWindow::draw_button(void* dis_v) {
    auto* dis = static_cast<DRAWITEMSTRUCT*>(dis_v);
    HDC dc = dis->hDC;
    RECT r = dis->rcItem;
    const int id = static_cast<int>(dis->CtlID);
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool is_nav = (id == IDC_NAV_LIVE || id == IDC_NAV_HISTORY ||
                         id == IDC_NAV_SETTINGS);

    wchar_t caption[64] = {};
    GetWindowTextW(dis->hwndItem, caption, 64);
    SetBkMode(dc, TRANSPARENT);

    if (is_nav) {
        const bool active = (id == IDC_NAV_LIVE);  // Live is the current view
        COLORREF bg = kSidebar;
        if (active) bg = kAccentBg;
        else if (pressed) bg = kBtn;
        // Fill the rail slot, then a rounded highlight for active/pressed.
        HBRUSH railb = CreateSolidBrush(kSidebar);
        FillRect(dc, &r, railb);
        DeleteObject(railb);
        if (active || pressed) fill_round(dc, r, bg, 10);
        SetTextColor(dc, active ? kAccentBright : kNavMuted);
        HGDIOBJ of = SelectObject(dc, static_cast<HFONT>(glyph_font_));
        DrawTextW(dc, caption, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(dc, of);
        return;
    }

    const bool primary = (id == IDC_TOGGLE);
    COLORREF fill = primary ? kAccent : kBtn;
    if (pressed) fill = primary ? RGB(36, 156, 108) : kBtnHot;
    const COLORREF txt = primary ? kOnAccent : kText;
    // Paint the corners with the window background so the rounded button has no
    // stray light corners showing through.
    HBRUSH bgb = CreateSolidBrush(kMain);
    FillRect(dc, &r, bgb);
    DeleteObject(bgb);
    fill_round(dc, r, fill, 8);
    SetTextColor(dc, txt);
    HGDIOBJ of = SelectObject(dc, static_cast<HFONT>(body_font_));
    DrawTextW(dc, caption, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, of);
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
        case IDC_NAV_HISTORY:
            if (callbacks_.on_open_history) callbacks_.on_open_history();
            break;
        case IDC_NAV_SETTINGS:
            if (callbacks_.on_open_settings) callbacks_.on_open_settings();
            break;
        case IDC_NAV_LIVE:
        default:
            break;
    }
}

void MainWindow::refresh_status() {
    if (toggle_btn_ == nullptr) return;
    SetWindowTextW(static_cast<HWND>(toggle_btn_),
                   is_recording(model_.status()) ? L"Pause" : L"Resume");
    InvalidateRect(static_cast<HWND>(toggle_btn_), nullptr, TRUE);
    // Repaint the title-bar recording pill.
    RECT tb{0, 0, kTitleH, kTitleH};
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
    if (display == last_display_) return;  // avoid resetting scroll/selection

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

void MainWindow::set_dark_theme(bool /*dark*/) {
    // The Studio design is dark-only; kept for interface compatibility.
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
