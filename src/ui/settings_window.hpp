#pragma once

// The Settings window (spec §252-§268, §591-§600): recording toggles, clipboard
// and window options, retention days, theme and startup options. It is seeded
// from a Configuration and hands the edited Configuration back through a
// callback on Save (the app then routes it to SettingsService). Thin Win32
// shell — the header avoids <Windows.h>; the Configuration model and validation
// are OS-free and tested.

#include <functional>

#include "core/configuration.hpp"

namespace ir {

class SettingsWindow {
public:
    using OnSave = std::function<void(const Configuration&)>;

    SettingsWindow() = default;

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    bool create(void* hinstance, void* owner_hwnd, const Configuration& initial,
                OnSave on_save);
    void show(int show_command);
    void* handle() const { return hwnd_; }

    long long handle_message(void* hwnd, unsigned msg, unsigned long long wparam,
                             long long lparam);

private:
    void on_create();
    void load_controls();          // Configuration -> controls
    Configuration read_controls();  // controls -> Configuration

    void* hwnd_ = nullptr;
    Configuration config_{};
    OnSave on_save_;
};

}  // namespace ir
