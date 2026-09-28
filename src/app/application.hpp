#pragma once

// Application assembly and lifecycle (spec §31, §462-§476, §566). Wires storage
// + startup recovery + the async writer + the reconstruction/snapshot coordinator
// + the capture sources + the main window + tray + global hotkey into one running
// app, and tears them down cleanly on exit (finalising the session so the next
// run does not see it as interrupted).
//
// This is the Windows integration layer (compiled-by-inspection off Windows).
// Every non-trivial decision it makes lives in the OS-free, unit-tested modules
// it composes.

#include <atomic>
#include <cstdint>
#include <memory>

#include "core/configuration.hpp"
#include "core/event_id_allocator.hpp"
#include "core/event_queue.hpp"
#include "core/ids.hpp"
#include "history/history_service.hpp"
#include "reliability/emergency_buffer.hpp"
#include "reliability/recording_coordinator.hpp"
#include "settings/settings_service.hpp"
#include "storage/event_store.hpp"
#include "storage/storage_worker.hpp"
#include "system/global_hotkey.hpp"
#include "ui/app_view_model.hpp"
#include "ui/history_window.hpp"
#include "ui/main_window.hpp"
#include "ui/settings_window.hpp"
#include "ui/tray_icon.hpp"

namespace ir {

class KeyboardHook;
class MouseHook;
class ClipboardMonitor;
class WindowContextMonitor;

class Application {
public:
    Application() = default;
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    // Opens storage, runs recovery, and brings up capture + UI. Returns false on
    // a fatal error (e.g. the database cannot be opened).
    bool initialize(void* hinstance, int show_command);

    // Runs the message loop until the app exits. Returns the exit code.
    int run();

    void shutdown();

    // Toggles recording on/off (called from the UI, tray or hotkey).
    void toggle_recording();

    // Pulls the coordinator's current view into the window (called on a timer).
    void refresh_ui();

private:
    bool open_storage();
    void start_capture();
    void stop_capture();
    void open_history();
    void open_settings();
    void apply_settings(const Configuration& config);
    void set_status(RecordingStatus status);

    void* hinstance_ = nullptr;
    unsigned long ui_thread_id_ = 0;

    EventStore store_;
    EmergencyBuffer emergency_;
    std::unique_ptr<SettingsService> settings_;
    std::unique_ptr<HistoryService> history_;
    std::unique_ptr<EventIdAllocator> ids_;
    std::unique_ptr<EventQueue> queue_;
    std::unique_ptr<StorageWorker> writer_;
    std::unique_ptr<RecordingCoordinator> coordinator_;

    std::unique_ptr<KeyboardHook> keyboard_;
    std::unique_ptr<MouseHook> mouse_;
    std::unique_ptr<ClipboardMonitor> clipboard_;
    std::unique_ptr<WindowContextMonitor> context_;

    MainWindow window_;
    TrayIcon tray_;
    GlobalHotkey hotkey_;
    std::unique_ptr<HistoryWindow> history_window_;
    std::unique_ptr<SettingsWindow> settings_window_;

    std::atomic<std::uint64_t> current_context_{0};  // shared context source
    SessionId session_{};
    RecordingStatus status_ = RecordingStatus::Recording;
    bool capture_running_ = false;
};

}  // namespace ir
