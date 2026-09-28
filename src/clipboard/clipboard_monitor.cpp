#include "clipboard/clipboard_monitor.hpp"

#include <Windows.h>

#include <future>
#include <optional>

#include "core/time.hpp"
#include "utils/unicode.hpp"

namespace ir {
namespace {

constexpr wchar_t kClassName[] = L"InputRecorderClipboardMonitor";

// Reads CF_UNICODETEXT from the clipboard with a bounded retry, because another
// app may briefly hold it open (spec §113). Returns nullopt if unavailable.
std::optional<std::string> read_clipboard_unicode_text(HWND owner) {
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (OpenClipboard(owner)) {
            std::optional<std::string> result;
            HANDLE handle = GetClipboardData(CF_UNICODETEXT);
            if (handle != nullptr) {
                auto* wide = static_cast<const wchar_t*>(GlobalLock(handle));
                if (wide != nullptr) {
                    std::u16string u16(reinterpret_cast<const char16_t*>(wide));
                    GlobalUnlock(handle);
                    result = utf16_to_utf8(u16);
                }
            }
            CloseClipboard();
            return result;  // may be nullopt if there was no text format
        }
        Sleep(10);  // clipboard busy; brief backoff
    }
    return std::nullopt;  // still locked after bounded retries
}

LRESULT CALLBACK ClipWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
    auto* self = reinterpret_cast<ClipboardMonitor*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_CLIPBOARDUPDATE && self != nullptr) {
        self->handle_clipboard_update();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

}  // namespace

ClipboardMonitor::ClipboardMonitor(EventQueue& queue, EventIdAllocator& ids,
                                   SessionId session, std::uint64_t max_bytes,
                                   EntrySink on_entry, std::uint64_t first_entry_id)
    : processor_(ids, session, max_bytes, first_entry_id),
      queue_(queue),
      on_entry_(std::move(on_entry)) {}

ClipboardMonitor::~ClipboardMonitor() { stop(); }

void ClipboardMonitor::note_self_copy(const std::string& utf8) {
    std::lock_guard<std::mutex> lock(proc_mutex_);
    processor_.note_self_copy(utf8);
}

void ClipboardMonitor::handle_clipboard_update() {
    auto text = read_clipboard_unicode_text(static_cast<HWND>(hwnd_));
    if (!text) {
        return;  // non-text content or clipboard unavailable; nothing to record
    }
    const ContextId context{current_context_.load(std::memory_order_relaxed)};

    ClipboardResult result;
    {
        std::lock_guard<std::mutex> lock(proc_mutex_);
        result = processor_.on_clipboard_text(*text, Clock::now_wall_ms(),
                                              Clock::now_monotonic_ns(), context);
    }
    if (result.suppressed) {
        return;
    }
    if (result.entry && on_entry_) {
        on_entry_(*result.entry);
    }
    if (result.event) {
        queue_.try_push(std::move(*result.event));
    }
    observed_.fetch_add(1, std::memory_order_relaxed);
}

void ClipboardMonitor::run() {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &ClipWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);  // ERROR_CLASS_ALREADY_EXISTS is fine

    HWND hwnd = CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                                nullptr, wc.hInstance, this);
    bool ok = (hwnd != nullptr) && (AddClipboardFormatListener(hwnd) != FALSE);
    hwnd_ = hwnd;
    thread_id_.store(GetCurrentThreadId(), std::memory_order_release);
    installed_.store(ok, std::memory_order_release);

    static_cast<std::promise<bool>*>(first_result_)->set_value(ok);

    if (!ok) {
        if (hwnd != nullptr) DestroyWindow(hwnd);
        hwnd_ = nullptr;
        return;
    }

    MSG msg;
    while (!stop_requested_.load(std::memory_order_acquire)) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) break;  // 0 == WM_QUIT
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    RemoveClipboardFormatListener(hwnd);
    DestroyWindow(hwnd);
    hwnd_ = nullptr;
    installed_.store(false, std::memory_order_release);
}

bool ClipboardMonitor::start() {
    if (started_) return installed_.load(std::memory_order_acquire);

    std::promise<bool> first_promise;
    std::future<bool> first_future = first_promise.get_future();
    first_result_ = &first_promise;

    started_ = true;
    stop_requested_.store(false, std::memory_order_release);
    thread_ = std::thread([this] { run(); });

    const bool ok = first_future.get();
    first_result_ = nullptr;
    return ok;
}

void ClipboardMonitor::stop() {
    if (!started_) return;
    stop_requested_.store(true, std::memory_order_release);

    const unsigned long tid = thread_id_.load(std::memory_order_acquire);
    if (tid != 0) {
        PostThreadMessageW(tid, WM_QUIT, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    started_ = false;
    thread_id_.store(0, std::memory_order_release);
}

}  // namespace ir
