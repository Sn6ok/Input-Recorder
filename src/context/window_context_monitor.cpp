#include "context/window_context_monitor.hpp"

#include <Windows.h>

#include <future>
#include <string>

#include "core/time.hpp"
#include "utils/unicode.hpp"

namespace ir {
namespace {

std::atomic<WindowContextMonitor*> g_active_monitor{nullptr};

void interruptible_sleep_ms(int total_ms, const std::atomic<bool>& stop) {
    const int step = 50;
    int slept = 0;
    while (slept < total_ms && !stop.load(std::memory_order_acquire)) {
        Sleep(static_cast<DWORD>(step));
        slept += step;
    }
}

// Win32 wide (UTF-16) string -> UTF-8, matching ClipboardMonitor's bridge.
std::string wide_to_utf8(const std::wstring& w) {
    return utf16_to_utf8(
        std::u16string(reinterpret_cast<const char16_t*>(w.data()), w.size()));
}

// Base name of a full image path ("C:\...\claude.exe" -> "claude.exe").
std::wstring base_name(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring process_image_name(DWORD pid) {
    if (pid == 0) return L"";
    HANDLE proc =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc == nullptr) return L"";
    wchar_t buffer[MAX_PATH];
    DWORD size = MAX_PATH;
    std::wstring result;
    if (QueryFullProcessImageNameW(proc, 0, buffer, &size)) {
        result.assign(buffer, size);
    }
    CloseHandle(proc);
    return base_name(result);
}

std::wstring window_title(HWND hwnd) {
    const int len = GetWindowTextLengthW(hwnd);
    if (len <= 0) return L"";
    std::wstring title(static_cast<size_t>(len) + 1, L'\0');
    const int copied = GetWindowTextW(hwnd, title.data(),
                                      static_cast<int>(title.size()));
    title.resize(static_cast<size_t>(copied < 0 ? 0 : copied));
    return title;
}

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject,
                           LONG idChild, DWORD, DWORD) {
    // Only foreground switches, or a name change on a top-level window.
    if (event == EVENT_OBJECT_NAMECHANGE &&
        (idObject != OBJID_WINDOW || idChild != CHILDID_SELF || hwnd == nullptr)) {
        return;
    }
    WindowContextMonitor* self = g_active_monitor.load(std::memory_order_acquire);
    if (self != nullptr) {
        self->handle_foreground_change();
    }
}

}  // namespace

WindowContextMonitor::WindowContextMonitor(EventQueue& queue,
                                           EventIdAllocator& ids,
                                           SessionId session,
                                           ContextSink on_context,
                                           std::uint64_t first_context_id)
    : queue_(queue),
      ids_(ids),
      session_(session),
      on_context_(std::move(on_context)),
      tracker_(session, first_context_id) {}

WindowContextMonitor::~WindowContextMonitor() { stop(); }

void WindowContextMonitor::observe_foreground() {
    HWND hwnd = GetForegroundWindow();
    if (hwnd == nullptr) return;  // no foreground (lock screen etc.) — skip

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    WindowObservation obs;
    obs.process_name = wide_to_utf8(process_image_name(pid));
    obs.window_title = wide_to_utf8(window_title(hwnd));
    obs.process_id = pid;
    obs.window_handle = reinterpret_cast<std::uint64_t>(hwnd);
    obs.wall_ms = Clock::now_wall_ms();

    ContextTracker::Update u = tracker_.observe(obs);
    current_context_.store(u.context.id.value, std::memory_order_release);
    if (!u.changed) return;

    changes_.fetch_add(1, std::memory_order_relaxed);
    if (on_context_) on_context_(u.context);

    Event e = tracker_.make_context_changed_event(u, Clock::now());
    e.id = ids_.next();
    queue_.try_push(std::move(e));
}

void WindowContextMonitor::handle_foreground_change() { observe_foreground(); }

bool WindowContextMonitor::install() {
    // A single range covering foreground + name changes, out-of-context so the
    // callback runs on this thread while it pumps messages, skipping our own
    // process's events.
    HWINEVENTHOOK h = SetWinEventHook(
        EVENT_SYSTEM_FOREGROUND, EVENT_OBJECT_NAMECHANGE, nullptr, &WinEventProc,
        0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    if (h == nullptr) return false;
    hook_handle_ = h;
    installed_.store(true, std::memory_order_release);
    return true;
}

void WindowContextMonitor::uninstall() {
    if (hook_handle_ != nullptr) {
        UnhookWinEvent(static_cast<HWINEVENTHOOK>(hook_handle_));
        hook_handle_ = nullptr;
    }
    installed_.store(false, std::memory_order_release);
}

bool WindowContextMonitor::retry_install() {
    static const int backoff_ms[] = {1000, 2000, 5000, 10000, 30000};
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (stop_requested_.load(std::memory_order_acquire)) return false;
        interruptible_sleep_ms(backoff_ms[attempt], stop_requested_);
        if (stop_requested_.load(std::memory_order_acquire)) return false;
        if (install()) return true;
    }
    return false;
}

void WindowContextMonitor::run() {
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
            push_diagnostic("window context hook unavailable (install failed)");
            return;
        }
    }

    // Establish a baseline context for whatever is already in the foreground.
    observe_foreground();

    while (!stop_requested_.load(std::memory_order_acquire)) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) break;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    uninstall();
}

bool WindowContextMonitor::start() {
    if (started_) return installed_.load(std::memory_order_acquire);

    WindowContextMonitor* expected = nullptr;
    if (!g_active_monitor.compare_exchange_strong(expected, this)) {
        push_diagnostic("window context monitor already active in this process");
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

void WindowContextMonitor::stop() {
    if (!started_) return;
    stop_requested_.store(true, std::memory_order_release);

    const unsigned long tid = thread_id_.load(std::memory_order_acquire);
    if (tid != 0) {
        PostThreadMessageW(tid, WM_QUIT, 0, 0);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    WindowContextMonitor* self = this;
    g_active_monitor.compare_exchange_strong(self, nullptr);
    started_ = false;
    thread_id_.store(0, std::memory_order_release);
}

void WindowContextMonitor::push_diagnostic(const char* message) {
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
