#include "app/application.hpp"

#include <Windows.h>
#include <commctrl.h>

#include <string>
#include <vector>

#include "capture/keyboard_hook.hpp"
#include "capture/mouse_hook.hpp"
#include "clipboard/clipboard_monitor.hpp"
#include "context/window_context_monitor.hpp"
#include "core/session.hpp"
#include "core/time.hpp"
#include "core/version.hpp"
#include "reliability/recovery.hpp"
#include "security/sensitive_input.hpp"
#include "storage/storage_paths.hpp"
#include "ui/history_formatting.hpp"
#include "ui/os_theme.hpp"
#include "ui/theme.hpp"

namespace ir {
namespace {

Application* g_app = nullptr;
constexpr UINT WM_APP_TOGGLE = WM_APP + 10;
constexpr UINT_PTR kRefreshTimerId = 1;
constexpr UINT kRefreshIntervalMs = 300;

MouseSettings mouse_settings_of(const Configuration& c) {
    MouseSettings ms;
    ms.record_buttons = c.record_mouse_clicks;
    ms.record_wheel = c.record_mouse_wheel;
    ms.record_movement = c.record_mouse_movement;
    ms.movement_sampling_ms = c.mouse_movement_sampling_ms;
    return ms;
}

SnapshotSettings snapshot_settings_of(const Configuration& c) {
    SnapshotSettings s;
    s.enabled = c.create_snapshots;
    s.event_interval = c.snapshot_event_interval;
    s.time_interval_ms = c.snapshot_time_interval_ms;
    return s;
}

void CALLBACK refresh_timer_proc(HWND, UINT, UINT_PTR, DWORD) {
    if (g_app != nullptr) g_app->refresh_ui();
}

}  // namespace

// Out-of-line so the members' unique_ptr<incomplete type> are instantiated here,
// where the capture-source types are complete.
Application::Application() = default;

Application::~Application() { shutdown(); }

bool Application::open_storage() {
    const std::string dir = default_data_dir();
    if (dir.empty()) return false;
    const std::string db = dir + "\\InputRecorder.db";
    if (!store_.open(db)) return false;

    // Startup recovery: flag any session left Active by a crashed run, and
    // replay events that only reached the emergency buffer (spec §462-§476).
    mark_interrupted_sessions(store_);
    emergency_.open(dir + "\\emergency.log");
    replay_emergency_buffer(emergency_, store_);
    return true;
}

bool Application::initialize(void* hinstance, int show_command) {
    hinstance_ = hinstance;
    ui_thread_id_ = GetCurrentThreadId();
    g_app = this;

    // Enable Common Controls v6 (visual styles for the list box, cue banner…).
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icc);

    if (!open_storage()) return false;

    settings_ = std::make_unique<SettingsService>(store_);
    settings_->load();
    Configuration config = settings_->config();

    history_ = std::make_unique<HistoryService>(store_);

    // Globally-unique id allocators seeded from what is already persisted.
    ids_ = std::make_unique<EventIdAllocator>(store_.max_event_id() + 1);
    session_ = SessionId{store_.max_session_id() + 1};

    Session s;
    s.id = session_;
    s.started_at_ms = Clock::now_wall_ms();
    s.status = SessionStatus::Active;
    s.app_version = version_string();
    store_.insert_session(s);

    queue_ = std::make_unique<EventQueue>(1u << 16);
    writer_ = std::make_unique<StorageWorker>(store_);
    writer_->start();

    RecordingCoordinator::Options opts;
    opts.snapshots = snapshot_settings_of(config);
    coordinator_ = std::make_unique<RecordingCoordinator>(
        *queue_, *writer_, store_, session_, opts, &emergency_);
    coordinator_->start();

    // Capture sources (created once; started/stopped by pause/resume). Their
    // context/clipboard id counters are seeded so ids stay unique across runs.
    keyboard_ = std::make_unique<KeyboardHook>(*queue_, *ids_, session_);
    keyboard_->set_context_source(&current_context_);
    keyboard_->set_sensitive_guard([] { return password_field_focused(); });

    mouse_ = std::make_unique<MouseHook>(*queue_, *ids_, session_,
                                         mouse_settings_of(config));
    mouse_->set_context_source(&current_context_);

    clipboard_ = std::make_unique<ClipboardMonitor>(
        *queue_, *ids_, session_, config.max_clipboard_item_bytes,
        [this](const ClipboardEntry& e) { writer_->submit_clipboard_entry(e); },
        store_.max_clipboard_id() + 1);

    context_ = std::make_unique<WindowContextMonitor>(
        *queue_, *ids_, session_,
        [this](const Context& c) {
            writer_->submit_context(c);
            current_context_.store(c.id.value, std::memory_order_release);
            if (clipboard_) clipboard_->set_current_context(c.id);
        },
        store_.max_context_id() + 1);

    // Main window.
    MainWindow::Callbacks wc;
    wc.on_recording_changed = [this](bool) { toggle_recording(); };
    wc.on_open_history = [this] { open_history(); };
    wc.on_open_settings = [this] { open_settings(); };
    wc.on_copy_all = [this](const std::string& text) {
        if (clipboard_) clipboard_->note_self_copy(text);
    };
    if (!window_.create(hinstance, wc)) return false;
    window_.set_close_to_tray(config.close_to_tray);
    window_.set_dark_theme(effective_dark(config.theme, system_prefers_dark()));

    // Tray icon.
    TrayIcon::Callbacks tc;
    tc.on_toggle_recording = [this] { toggle_recording(); };
    tc.on_show_hide = [this] {
        HWND h = static_cast<HWND>(window_.handle());
        if (IsWindowVisible(h)) {
            ShowWindow(h, SW_HIDE);
        } else {
            ShowWindow(h, SW_SHOW);
            SetForegroundWindow(h);
        }
    };
    tc.on_settings = [this] { open_settings(); };
    tc.on_exit = [] { PostQuitMessage(0); };
    tc.window_visible = [this] {
        return IsWindowVisible(static_cast<HWND>(window_.handle())) != FALSE;
    };
    tc.status = [this] { return status_; };
    tray_.create(hinstance, tc);

    // Global toggle hotkey (marshalled to the UI thread).
    hotkey_.start(config.toggle_hotkey, [this] {
        PostThreadMessageW(ui_thread_id_, WM_APP_TOGGLE, 0, 0);
    });

    status_ = config.enable_recording ? RecordingStatus::Recording
                                      : RecordingStatus::Paused;
    if (config.enable_recording) start_capture();
    set_status(status_);

    window_.show(config.start_minimized ? SW_HIDE : show_command);
    SetTimer(static_cast<HWND>(window_.handle()), kRefreshTimerId,
             kRefreshIntervalMs, nullptr);
    // Drive the periodic UI refresh even when the window is hidden.
    SetTimer(nullptr, 0, kRefreshIntervalMs, &refresh_timer_proc);
    return true;
}

void Application::start_capture() {
    if (capture_running_) return;
    const Configuration c = settings_->config();
    if (c.record_keyboard) keyboard_->start();
    if (c.record_mouse_clicks || c.record_mouse_wheel || c.record_mouse_movement)
        mouse_->start();
    if (c.monitor_clipboard) clipboard_->start();
    if (c.track_active_window) context_->start();
    capture_running_ = true;
}

void Application::stop_capture() {
    if (!capture_running_) return;
    if (keyboard_) keyboard_->stop();
    if (mouse_) mouse_->stop();
    if (clipboard_) clipboard_->stop();
    if (context_) context_->stop();
    capture_running_ = false;
}

void Application::set_status(RecordingStatus status) {
    status_ = status;
    window_.set_status(status);
    tray_.refresh();
}

void Application::toggle_recording() {
    if (capture_running_) {
        stop_capture();
        set_status(RecordingStatus::Paused);
    } else {
        start_capture();
        set_status(RecordingStatus::Recording);
    }
    // Persist the recording preference.
    if (settings_) {
        settings_->edit([this](Configuration& c) {
            c.enable_recording = (status_ == RecordingStatus::Recording);
        });
    }
}

void Application::refresh_ui() {
    if (!coordinator_) return;
    const RecordingCoordinator::View v = coordinator_->view();
    window_.set_reconstruction(v.text, v.annotated, v.confidence);
}

void Application::open_history() {
    history_window_ = std::make_unique<HistoryWindow>();
    auto source = [this](const std::string& query) -> std::vector<std::string> {
        std::vector<std::string> rows;
        if (query.empty()) {
            EventQuery q;
            q.limit = 500;
            q.newest_first = true;
            for (const Event& e : history_->query_events(q))
                rows.push_back(format_event_row(e));
        } else {
            for (const Event& e :
                 history_->search_events(query, std::nullopt, 500, 0))
                rows.push_back(format_event_row(e));
        }
        return rows;
    };
    if (history_window_->create(hinstance_, window_.handle(), source)) {
        history_window_->show(SW_SHOW);
    }
}

void Application::open_settings() {
    settings_window_ = std::make_unique<SettingsWindow>();
    if (settings_window_->create(hinstance_, window_.handle(), settings_->config(),
                                 [this](const Configuration& c) {
                                     apply_settings(c);
                                 })) {
        settings_window_->show(SW_SHOW);
    }
}

void Application::apply_settings(const Configuration& config) {
    const bool was_recording = (status_ == RecordingStatus::Recording);
    settings_->update(config);

    if (mouse_) mouse_->set_settings(mouse_settings_of(config));
    window_.set_close_to_tray(config.close_to_tray);
    window_.set_dark_theme(effective_dark(config.theme, system_prefers_dark()));
    hotkey_.rebind(config.toggle_hotkey);

    // Apply an enable-recording change.
    if (config.enable_recording && !was_recording) {
        start_capture();
        set_status(RecordingStatus::Recording);
    } else if (!config.enable_recording && was_recording) {
        stop_capture();
        set_status(RecordingStatus::Paused);
    } else if (was_recording) {
        // Restart capture so per-source toggles (keyboard/clipboard/etc.) apply.
        stop_capture();
        start_capture();
    }
}

int Application::run() {
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == nullptr && msg.message == WM_APP_TOGGLE) {
            toggle_recording();
            continue;
        }
        if (!IsDialogMessageW(static_cast<HWND>(window_.handle()), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return static_cast<int>(msg.wParam);
}

void Application::shutdown() {
    if (g_app == this) g_app = nullptr;
    KillTimer(nullptr, 0);

    hotkey_.stop();
    stop_capture();
    if (coordinator_) coordinator_->stop();  // closes queue, drains remainder
    if (writer_) {
        writer_->flush();
        writer_->stop();
    }
    if (store_.is_open() && session_.valid()) {
        store_.finalize_session(session_, Clock::now_wall_ms(),
                                SessionStatus::Completed);
    }
    emergency_.close();
    tray_.destroy();
    store_.close();
}

}  // namespace ir
