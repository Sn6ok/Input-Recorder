#pragma once

// System-tray presence (spec §607-§615): an icon whose tooltip reflects the
// recording state, with a context menu (Pause/Resume, Show/Hide, Settings,
// Exit) and double-click to show/hide the window. Uses a message-only window to
// receive the tray callback and menu commands. Header avoids <Windows.h>; the
// menu labels/tooltip come from the OS-free tray_menu_model.

#include <functional>

#include "ui/app_view_model.hpp"

namespace ir {

class TrayIcon {
public:
    struct Callbacks {
        std::function<void()> on_toggle_recording;
        std::function<void()> on_show_hide;
        std::function<void()> on_settings;
        std::function<void()> on_exit;
        std::function<bool()> window_visible;      // for the Show/Hide label
        std::function<RecordingStatus()> status;   // for the tooltip + label
    };

    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool create(void* hinstance, Callbacks callbacks);
    void destroy();

    // Refreshes the tooltip/icon to match the current recording state.
    void refresh();

    long long handle_message(void* hwnd, unsigned msg, unsigned long long wparam,
                             long long lparam);

private:
    void show_menu();
    RecordingStatus current_status() const;

    void* hwnd_ = nullptr;
    void* hinstance_ = nullptr;
    Callbacks cb_;
    bool added_ = false;
};

}  // namespace ir
