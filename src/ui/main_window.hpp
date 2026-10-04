#pragma once

// The single application window — "Studio" layout (spec §226-§301): a custom
// frameless dark window with a left navigation rail (Live / History / Settings)
// that switches between three in-window views (no separate windows):
//   * Live     — reconstructed text with inline key/shortcut markers + Pause/Copy
//   * History  — search box + results list
//   * Settings — recording toggles, theme, retention + Save
//
// Display state comes from the OS-free AppViewModel; the app supplies data via
// callbacks. Header avoids <Windows.h>. App -> UI updates run on the UI thread.

#include <functional>
#include <string>
#include <vector>

#include "core/configuration.hpp"
#include "core/confidence.hpp"
#include "ui/app_view_model.hpp"

namespace ir {

class MainWindow {
public:
    struct Callbacks {
        std::function<void(bool recording)> on_recording_changed;
        std::function<void(const std::string& copied_utf8)> on_copy_all;
        // History view data source: rows for the given search query ("" = all).
        std::function<std::vector<std::string>(const std::string& query)> history_source;
        // Settings view: current config, and apply-on-save.
        std::function<Configuration()> get_config;
        std::function<void(const Configuration&)> on_save_settings;
    };

    enum View { kViewLive = 0, kViewHistory = 1, kViewSettings = 2 };

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

    // Switch the active in-window view (also used by the tray's Settings item).
    void activate_view(int view);

    const AppViewModel& model() const { return model_; }

    int run_message_loop();

    long long handle_message(void* hwnd, unsigned msg, unsigned long long wparam,
                             long long lparam);

private:
    void on_create();
    void on_size(int width, int height);
    void on_command(int control_id, int notify);
    void refresh_status();
    void refresh_text();
    void do_copy_all();
    void apply_theme();
    void draw_button(void* draw_item_struct);
    void paint_chrome();
    int button_at(int x, int y) const;
    void title_button_action(int button);
    long long hit_test(long long lparam);

    void build_history_controls(void* inst);
    void build_settings_controls(void* inst);
    void refresh_history();
    void load_settings_controls();
    Configuration read_settings_controls() const;
    void layout_content(int width, int height);
    int sidebar_w() const;   // 0 in compact mode, kSidebarW otherwise
    void toggle_compact();   // compact mode: just the recovered text

    void* hwnd_ = nullptr;
    // Live view
    void* note_label_ = nullptr;
    void* text_view_ = nullptr;
    void* toggle_btn_ = nullptr;
    void* copy_btn_ = nullptr;
    // History view
    void* search_edit_ = nullptr;
    void* list_box_ = nullptr;
    // Settings view (all controls incl. labels, tracked for show/hide)
    std::vector<void*> settings_ctrls_;
    // Sidebar
    void* nav_live_ = nullptr;
    void* nav_history_ = nullptr;
    void* nav_settings_ = nullptr;

    void* bg_brush_ = nullptr;
    void* sidebar_brush_ = nullptr;
    void* panel_brush_ = nullptr;
    void* body_font_ = nullptr;
    void* title_font_ = nullptr;
    void* glyph_font_ = nullptr;

    int view_ = kViewLive;
    bool compact_ = false;
    std::string last_display_;
    int hot_btn_ = 0;
    int pressed_btn_ = 0;
    bool dark_ = true;
    bool close_to_tray_ = false;

    AppViewModel model_;
    Callbacks callbacks_;
};

}  // namespace ir
