#pragma once

// The main application window — "Studio" layout (spec §226-§301): a custom
// frameless dark window with a left navigation rail (Live / History / Settings),
// a title bar with a recording pill and window controls, a read-only view of the
// reconstructed text with inline key/shortcut markers, and Pause + Copy All.
//
// All display strings/state come from the OS-free AppViewModel; this file is a
// thin Win32 shell (header avoids <Windows.h>). App -> UI updates
// (set_status/set_reconstruction) must run on the UI thread.

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
        std::function<void()> on_open_settings;
        std::function<void(const std::string& copied_utf8)> on_copy_all;  // self-copy
    };

    MainWindow() = default;
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool create(void* hinstance, Callbacks callbacks);
    void show(int show_command);
    void* handle() const { return hwnd_; }

    void set_status(RecordingStatus status);
    void set_reconstruction(const std::string& text, const std::string& annotated,
                            Confidence confidence);
    void set_dark_theme(bool dark);
    void set_close_to_tray(bool value) { close_to_tray_ = value; }

    const AppViewModel& model() const { return model_; }

    int run_message_loop();

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
    void draw_button(void* draw_item_struct);  // WM_DRAWITEM owner-draw
    void paint_chrome();                        // title bar + sidebar (WM_PAINT)
    int button_at(int x, int y) const;          // 1=min, 2=close, 0=none
    void title_button_action(int button);
    long long hit_test(long long lparam);       // WM_NCHITTEST

    void* hwnd_ = nullptr;
    void* note_label_ = nullptr;    // confidence note (static)
    void* text_view_ = nullptr;     // read-only edit
    void* toggle_btn_ = nullptr;    // Pause / Resume
    void* copy_btn_ = nullptr;      // Copy All
    void* nav_live_ = nullptr;      // sidebar: Live (active)
    void* nav_history_ = nullptr;   // sidebar: History
    void* nav_settings_ = nullptr;  // sidebar: Settings

    void* bg_brush_ = nullptr;       // main area background
    void* sidebar_brush_ = nullptr;  // navigation rail background
    void* panel_brush_ = nullptr;    // text panel background
    void* body_font_ = nullptr;      // Segoe UI body
    void* title_font_ = nullptr;     // Segoe UI title / pill
    void* glyph_font_ = nullptr;     // Segoe UI Symbol for nav glyphs

    std::string last_display_;
    int hot_btn_ = 0;
    int pressed_btn_ = 0;
    bool dark_ = true;
    bool close_to_tray_ = false;

    AppViewModel model_;
    Callbacks callbacks_;
};

}  // namespace ir
