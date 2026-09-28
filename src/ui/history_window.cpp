#include "ui/history_window.hpp"

#include <Windows.h>

#include "utils/unicode.hpp"

namespace ir {
namespace {

constexpr wchar_t kClassName[] = L"InputRecorderHistoryWindow";
constexpr int IDC_SEARCH = 2001;
constexpr int IDC_LIST = 2002;

std::wstring to_w(const std::string& utf8) {
    std::u16string u16 = utf8_to_utf16(utf8);
    return std::wstring(reinterpret_cast<const wchar_t*>(u16.data()), u16.size());
}

std::string from_w(HWND edit) {
    const int len = GetWindowTextLengthW(edit);
    if (len <= 0) return {};
    std::wstring buf(static_cast<size_t>(len) + 1, L'\0');
    const int n = GetWindowTextW(edit, buf.data(), static_cast<int>(buf.size()));
    buf.resize(static_cast<size_t>(n < 0 ? 0 : n));
    return utf16_to_utf8(
        std::u16string(reinterpret_cast<const char16_t*>(buf.data()), buf.size()));
}

}  // namespace

LRESULT CALLBACK HistoryWindowProc(HWND hwnd, UINT msg, WPARAM wparam,
                                   LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self =
        reinterpret_cast<HistoryWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self != nullptr) {
        return static_cast<LRESULT>(self->handle_message(
            hwnd, msg, static_cast<unsigned long long>(wparam),
            static_cast<long long>(lparam)));
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

bool HistoryWindow::create(void* hinstance, void* owner_hwnd, DataSource source) {
    source_ = std::move(source);
    HINSTANCE inst = static_cast<HINSTANCE>(hinstance);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &HistoryWindowProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, kClassName, L"History", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, 560, 520, static_cast<HWND>(owner_hwnd), nullptr, inst,
        this);
    hwnd_ = hwnd;
    return hwnd != nullptr;
}

void HistoryWindow::show(int show_command) {
    if (hwnd_ != nullptr) {
        ShowWindow(static_cast<HWND>(hwnd_), show_command);
        UpdateWindow(static_cast<HWND>(hwnd_));
    }
}

long long HistoryWindow::handle_message(void* hwnd_v, unsigned msg,
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
            // Live search: repopulate whenever the query text changes.
            if (LOWORD(wparam) == IDC_SEARCH && HIWORD(wparam) == EN_CHANGE) {
                refresh();
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, static_cast<WPARAM>(wparam),
                          static_cast<LPARAM>(lparam));
}

void HistoryWindow::on_create() {
    HWND hwnd = static_cast<HWND>(hwnd_);
    HINSTANCE inst =
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

    search_edit_ = CreateWindowExW(
        0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, 0, 0,
        0, 0, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SEARCH)),
        inst, nullptr);
    list_box_ = CreateWindowExW(
        0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | LBS_NOINTEGRALHEIGHT |
            LBS_HASSTRINGS,
        0, 0, 0, 0, hwnd,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIST)), inst, nullptr);

    SendMessageW(static_cast<HWND>(search_edit_), WM_SETFONT,
                 reinterpret_cast<WPARAM>(font), TRUE);
    SendMessageW(static_cast<HWND>(list_box_), WM_SETFONT,
                 reinterpret_cast<WPARAM>(font), TRUE);

    // Placeholder/cue text for the search box.
    SendMessageW(static_cast<HWND>(search_edit_), EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"Search recorded text…"));
    refresh();  // initial (empty query) population
}

void HistoryWindow::on_size(int width, int height) {
    const int pad = 10;
    const int search_h = 24;
    MoveWindow(static_cast<HWND>(search_edit_), pad, pad, width - 2 * pad,
               search_h, TRUE);
    const int list_y = pad + search_h + pad;
    MoveWindow(static_cast<HWND>(list_box_), pad, list_y, width - 2 * pad,
               (height - list_y - pad > 0) ? height - list_y - pad : 0, TRUE);
}

void HistoryWindow::refresh() {
    if (list_box_ == nullptr || !source_) return;
    const std::string query = from_w(static_cast<HWND>(search_edit_));
    const std::vector<std::string> rows = source_(query);

    HWND list = static_cast<HWND>(list_box_);
    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const std::string& row : rows) {
        const std::wstring w = to_w(row);
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(w.c_str()));
    }
    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, nullptr, TRUE);
}

}  // namespace ir
