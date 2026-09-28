#pragma once

// The main application window (spec §566-§575): a visible recording indicator
// (● RECORDING / ○ PAUSED), a read-only multi-line view of the reconstructed
// text, a confidence note, and Pause/Resume, Copy All and History buttons.
// Supports a dark or light palette (finalised in Phase 11).
//
// All display strings/state come from the OS-free AppViewModel and
// history_formatting, so this file is a thin Win32 shell (header avoids
// <Windows.h>). Updates from the app (set_status/set_reconstruction) must be
// made on the UI thread; the app marshals to it with PostMessage.

#include <functional>
#include <string>

#include "core/confidence.hpp"
#include "ui/app_view_model.hpp"

namespace ir {

class MainWindow {
public:
    struct Callbacks {
        std::function<void(bool recording)> on_recording_changed;  // user toggle
        std::function<void()> on_open_history;
        std::function<void(const std::string& copied_utf8)> on_copy_all;  // self-copy
    };

    MainWindow() = default;
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // Registers the class (once) and creates the window. `hinstance` is an
    // HINSTANCE. Returns false on failure.
    bool create(void* hinstance, Callbacks callbacks);
    void show(int show_command);
    void* handle() const { return hwnd_; }

    // App -> UI updates (call on the UI thread).
    void set_status(RecordingStatus status);
    void set_reconstruction(const std::string& text, Confidence confidence);
    void set_dark_theme(bool dark);

    const AppViewModel& model() const { return model_; }

    // Runs a standard message loop until the window closes. Convenience for a
    // UI-only launch; the full app owns its own loop.
    int run_message_loop();

    // Internal: window procedure dispatch (used by the file-scope WndProc).
    long long handle_message(void* hwnd, unsigned msg, unsigned long long wparam,
                             long long lparam);

private:
    void on_create();
    void on_size(int width, int height);
    void on_command(int control_id);
    void refresh_status();
    void refresh_text();
    void do_copy_all();
    void apply_theme();

    void* hwnd_ = nullptr;         // HWND
    void* status_label_ = nullptr; // HWND (static)
    void* note_label_ = nullptr;   // HWND (static)
    void* text_view_ = nullptr;    // HWND (read-only edit)
    void* toggle_btn_ = nullptr;   // HWND
    void* copy_btn_ = nullptr;     // HWND
    void* history_btn_ = nullptr;  // HWND
    void* bg_brush_ = nullptr;     // HBRUSH for the themed background
    bool dark_ = false;

    AppViewModel model_;
    Callbacks callbacks_;
};

}  // namespace ir
