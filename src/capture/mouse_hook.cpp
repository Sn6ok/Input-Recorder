#include "capture/mouse_hook.hpp"

#include <Windows.h>

#include <future>

#include "capture/raw_input.hpp"
#include "core/time.hpp"

namespace ir {
namespace {

// Only one active mouse hook per process (spec §412). The low-level callback is
// a free function; it reaches the owning hook through this pointer.
std::atomic<MouseHook*> g_active_hook{nullptr};

void interruptible_sleep_ms(int total_ms, const std::atomic<bool>& stop) {
    const int step = 50;
    int slept = 0;
    while (slept < total_ms && !stop.load(std::memory_order_acquire)) {
        Sleep(static_cast<DWORD>(step));
        slept += step;
    }
}

// Maps a low-level mouse message + MSLLHOOKSTRUCT into an OS-neutral input.
// Returns false for messages we do not record (leaves `out` unspecified).
bool decode(WPARAM message, const MSLLHOOKSTRUCT& m, RawMouseInput& out) {
    out.x = m.pt.x;
    out.y = m.pt.y;
    out.injected = (m.flags & LLMHF_INJECTED) != 0;
    out.button = MouseButton::None;
    out.wheel_delta = 0;

    switch (message) {
        case WM_MOUSEMOVE:
            out.action = RawMouseAction::Move;
            return true;
        case WM_LBUTTONDOWN:
            out.action = RawMouseAction::ButtonDown;
            out.button = MouseButton::Left;
            return true;
        case WM_LBUTTONUP:
            out.action = RawMouseAction::ButtonUp;
            out.button = MouseButton::Left;
            return true;
        case WM_RBUTTONDOWN:
            out.action = RawMouseAction::ButtonDown;
            out.button = MouseButton::Right;
            return true;
        case WM_RBUTTONUP:
            out.action = RawMouseAction::ButtonUp;
            out.button = MouseButton::Right;
            return true;
        case WM_MBUTTONDOWN:
            out.action = RawMouseAction::ButtonDown;
            out.button = MouseButton::Middle;
            return true;
        case WM_MBUTTONUP:
            out.action = RawMouseAction::ButtonUp;
            out.button = MouseButton::Middle;
            return true;
        case WM_XBUTTONDOWN:
            out.action = RawMouseAction::ButtonDown;
            out.button = (GET_XBUTTON_WPARAM(m.mouseData) == XBUTTON1)
                             ? MouseButton::X1
                             : MouseButton::X2;
            return true;
        case WM_XBUTTONUP:
            out.action = RawMouseAction::ButtonUp;
            out.button = (GET_XBUTTON_WPARAM(m.mouseData) == XBUTTON1)
                             ? MouseButton::X1
                             : MouseButton::X2;
            return true;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            out.action = RawMouseAction::Wheel;
            out.wheel_delta = GET_WHEEL_DELTA_WPARAM(m.mouseData);
            return true;
        default:
            return false;
    }
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        MouseHook* self = g_active_hook.load(std::memory_order_acquire);
        if (self != nullptr) {
            const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);
            RawMouseInput raw;
            if (decode(wParam, *m, raw)) {
                self->on_raw_input(raw);
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

}  // namespace

MouseHook::MouseHook(EventQueue& queue, EventIdAllocator& ids, SessionId session,
                     const MouseSettings& settings)
    : queue_(queue), ids_(ids), session_(session), processor_(settings) {}

MouseHook::~MouseHook() { stop(); }

void MouseHook::set_settings(const MouseSettings& settings) {
    std::lock_guard<std::mutex> lock(proc_mutex_);
    processor_.set_settings(settings);
}

void MouseHook::on_raw_input(const RawMouseInput& raw) {
    // Runs on the capture thread only. Keep it minimal (spec §58, §455).
    std::optional<Event> e;
    {
        std::lock_guard<std::mutex> lock(proc_mutex_);
        e = processor_.process(raw, Clock::now());
    }
    if (!e) return;  // filtered (movement off / throttled / category disabled)
    e->id = ids_.next();
    e->session = session_;
    if (context_source_ != nullptr) {
        e->context = ContextId{context_source_->load(std::memory_order_relaxed)};
    }
    if (queue_.try_push(std::move(*e))) {
        captured_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool MouseHook::install() {
    HINSTANCE module = GetModuleHandleW(nullptr);
    HHOOK handle = SetWindowsHookExW(WH_MOUSE_LL, &LowLevelMouseProc, module, 0);
    if (handle == nullptr) {
        return false;
    }
    hook_handle_ = handle;
    installed_.store(true, std::memory_order_release);
    return true;
}

void MouseHook::uninstall() {
    if (hook_handle_ != nullptr) {
        UnhookWindowsHookEx(static_cast<HHOOK>(hook_handle_));
        hook_handle_ = nullptr;
    }
    installed_.store(false, std::memory_order_release);
}

bool MouseHook::retry_install() {
    static const int backoff_ms[] = {1000, 2000, 5000, 10000, 30000};
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (stop_requested_.load(std::memory_order_acquire)) return false;
        interruptible_sleep_ms(backoff_ms[attempt], stop_requested_);
        if (stop_requested_.load(std::memory_order_acquire)) return false;
        if (install()) return true;
    }
    return false;
}

void MouseHook::run() {
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    thread_id_.store(GetCurrentThreadId(), std::memory_order_release);

    const bool first = install();
    {
        std::promise<bool>* p = static_cast<std::promise<bool>*>(first_result_);
        p->set_value(first);
    }

    if (!first) {
        if (!retry_install()) {
            push_diagnostic("mouse hook unavailable (install failed)");
            return;
        }
    }

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

bool MouseHook::start() {
    if (started_) return installed_.load(std::memory_order_acquire);

    MouseHook* expected = nullptr;
    if (!g_active_hook.compare_exchange_strong(expected, this)) {
        push_diagnostic("mouse hook already active in this process");
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

void MouseHook::stop() {
    if (!started_) return;
    stop_requested_.store(true, std::memory_order_release);

    const unsigned long tid = thread_id_.load(std::memory_order_acquire);
    if (tid != 0) {
        PostThreadMessageW(tid, WM_QUIT, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    MouseHook* self = this;
    g_active_hook.compare_exchange_strong(self, nullptr);
    started_ = false;
    thread_id_.store(0, std::memory_order_release);
}

void MouseHook::push_diagnostic(const char* message) {
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
