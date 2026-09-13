#include "capture/keyboard_hook.hpp"

#include <Windows.h>

#include <future>

#include "capture/raw_input.hpp"
#include "core/time.hpp"

namespace ir {
namespace {

// Only one active keyboard hook per process (spec §412). The low-level callback
// is a free function; it reaches the owning hook through this pointer.
std::atomic<KeyboardHook*> g_active_hook{nullptr};

// Interruptible sleep: returns early if `stop` becomes true.
void interruptible_sleep_ms(int total_ms, const std::atomic<bool>& stop) {
    const int step = 50;
    int slept = 0;
    while (slept < total_ms && !stop.load(std::memory_order_acquire)) {
        Sleep(static_cast<DWORD>(step));
        slept += step;
    }
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KeyboardHook* self = g_active_hook.load(std::memory_order_acquire);
        if (self != nullptr) {
            const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
            RawKeyboardInput raw;
            raw.virtual_key = k->vkCode;
            raw.scan_code = k->scanCode;
            raw.key_up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
            raw.extended = (k->flags & LLKHF_EXTENDED) != 0;
            raw.injected = (k->flags & LLKHF_INJECTED) != 0;
            self->on_raw_input(raw);
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}  // namespace

KeyboardHook::KeyboardHook(EventQueue& queue, EventIdAllocator& ids, SessionId session)
    : queue_(queue), ids_(ids), session_(session) {}

KeyboardHook::~KeyboardHook() { stop(); }

void KeyboardHook::on_raw_input(const RawKeyboardInput& raw) {
    // Runs on the capture thread only. Keep it minimal (spec §58, §455).
    Event e = translator_.translate(raw, Clock::now());
    e.id = ids_.next();
    e.session = session_;
    if (queue_.try_push(std::move(e))) {
        captured_.fetch_add(1, std::memory_order_relaxed);
    }
    // On a full queue the event is dropped and counted by the queue itself
    // (overflow flag); we do not block system input here.
}

bool KeyboardHook::install() {
    HINSTANCE module = GetModuleHandleW(nullptr);
    HHOOK handle = SetWindowsHookExW(WH_KEYBOARD_LL, &LowLevelKeyboardProc, module, 0);
    if (handle == nullptr) {
        return false;
    }
    hook_handle_ = handle;
    installed_.store(true, std::memory_order_release);
    return true;
}

void KeyboardHook::uninstall() {
    if (hook_handle_ != nullptr) {
        UnhookWindowsHookEx(static_cast<HHOOK>(hook_handle_));
        hook_handle_ = nullptr;
    }
    installed_.store(false, std::memory_order_release);
}

bool KeyboardHook::retry_install() {
    // Bounded backoff, interruptible; never an unbounded spin (spec §384, §385).
    static const int backoff_ms[] = {1000, 2000, 5000, 10000, 30000};
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (stop_requested_.load(std::memory_order_acquire)) return false;
        interruptible_sleep_ms(backoff_ms[attempt], stop_requested_);
        if (stop_requested_.load(std::memory_order_acquire)) return false;
        if (install()) return true;
    }
    return false;
}

void KeyboardHook::run() {
    // Force message-queue creation so stop() may PostThreadMessage safely.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    thread_id_.store(GetCurrentThreadId(), std::memory_order_release);

    const bool first = install();
    {
        // Signal start()'s waiter with the first-attempt result.
        std::promise<bool>* p = static_cast<std::promise<bool>*>(first_result_);
        p->set_value(first);
    }

    if (!first) {
        if (!retry_install()) {
            push_diagnostic("keyboard hook unavailable (install failed)");
            return;
        }
    }

    // Message loop: low-level hook callbacks fire on this thread while pumping.
    while (!stop_requested_.load(std::memory_order_acquire)) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {  // 0 == WM_QUIT, -1 == error
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    uninstall();
}

bool KeyboardHook::start() {
    if (started_) return installed_.load(std::memory_order_acquire);

    // Enforce single active instance (spec §412).
    KeyboardHook* expected = nullptr;
    if (!g_active_hook.compare_exchange_strong(expected, this)) {
        push_diagnostic("keyboard hook already active in this process");
        return false;
    }

    std::promise<bool> first_promise;
    std::future<bool> first_future = first_promise.get_future();
    first_result_ = &first_promise;

    started_ = true;
    stop_requested_.store(false, std::memory_order_release);
    thread_ = std::thread([this] { run(); });

    const bool installed = first_future.get();
    first_result_ = nullptr;
    return installed;
}

void KeyboardHook::stop() {
    if (!started_) return;
    stop_requested_.store(true, std::memory_order_release);

    const unsigned long tid = thread_id_.load(std::memory_order_acquire);
    if (tid != 0) {
        PostThreadMessageW(tid, WM_QUIT, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    // Clear the active-instance pointer only if it still refers to us.
    KeyboardHook* self = this;
    g_active_hook.compare_exchange_strong(self, nullptr);
    started_ = false;
    thread_id_.store(0, std::memory_order_release);
}

void KeyboardHook::push_diagnostic(const char* message) {
    Event e;
    e.id = ids_.next();
    e.session = session_;
    e.type = EventType::Diagnostic;
    e.category = EventCategory::SystemEvent;
    e.time = Clock::now();
    e.payload = DiagnosticData{message};
    queue_.try_push(std::move(e));
}

}  // namespace ir
