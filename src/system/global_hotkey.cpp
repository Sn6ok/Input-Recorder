#include "system/global_hotkey.hpp"

#include <Windows.h>

#include <future>

#include "system/hotkey.hpp"

namespace ir {
namespace {
constexpr int kHotkeyId = 1;
}

GlobalHotkey::~GlobalHotkey() { stop(); }

void GlobalHotkey::run(Hotkey hotkey) {
    // Create the thread message queue before anyone posts to it.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    thread_id_.store(GetCurrentThreadId(), std::memory_order_release);

    const UINT mods = to_win32_modifiers(hotkey);
    const bool ok =
        is_registerable(hotkey) &&
        RegisterHotKey(nullptr, kHotkeyId, mods, hotkey.virtual_key) != FALSE;
    active_.store(ok, std::memory_order_release);

    static_cast<std::promise<bool>*>(first_result_)->set_value(ok);
    if (!ok) return;

    while (!stop_requested_.load(std::memory_order_acquire)) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) break;  // WM_QUIT / error
        if (msg.message == WM_HOTKEY && msg.wParam == kHotkeyId) {
            if (callback_) callback_();
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    UnregisterHotKey(nullptr, kHotkeyId);
    active_.store(false, std::memory_order_release);
}

bool GlobalHotkey::start(const Hotkey& hotkey, Callback on_pressed) {
    if (started_) return active_.load(std::memory_order_acquire);
    callback_ = std::move(on_pressed);

    std::promise<bool> first_promise;
    std::future<bool> first_future = first_promise.get_future();
    first_result_ = &first_promise;

    started_ = true;
    stop_requested_.store(false, std::memory_order_release);
    thread_ = std::thread([this, hotkey] { run(hotkey); });

    const bool ok = first_future.get();
    first_result_ = nullptr;
    return ok;
}

void GlobalHotkey::stop() {
    if (!started_) return;
    stop_requested_.store(true, std::memory_order_release);
    const unsigned long tid = thread_id_.load(std::memory_order_acquire);
    if (tid != 0) PostThreadMessageW(tid, WM_QUIT, 0, 0);
    if (thread_.joinable()) thread_.join();
    started_ = false;
    thread_id_.store(0, std::memory_order_release);
}

bool GlobalHotkey::rebind(const Hotkey& hotkey) {
    Callback cb = callback_;
    stop();
    return start(hotkey, std::move(cb));
}

}  // namespace ir
