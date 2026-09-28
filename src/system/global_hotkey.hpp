#pragma once

// Global toggle hotkey via RegisterHotKey (spec §268, §601). Runs a dedicated
// thread that owns the RegisterHotKey registration and a message loop; on
// WM_HOTKEY it invokes the callback (which the app marshals to the UI thread).
// Header avoids <Windows.h>. If the hotkey is already taken by another app the
// registration fails gracefully and active() stays false (spec §605).

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "core/configuration.hpp"

namespace ir {

class GlobalHotkey {
public:
    using Callback = std::function<void()>;

    GlobalHotkey() = default;
    ~GlobalHotkey();

    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;

    // Registers `hotkey` and calls `on_pressed` each time it fires. Returns true
    // if the registration succeeded.
    bool start(const Hotkey& hotkey, Callback on_pressed);
    void stop();

    // Re-registers with a new hotkey, keeping the same callback.
    bool rebind(const Hotkey& hotkey);

    bool active() const { return active_.load(std::memory_order_acquire); }

private:
    void run(Hotkey hotkey);

    Callback callback_;
    std::thread thread_;
    std::atomic<unsigned long> thread_id_{0};
    std::atomic<bool> active_{false};
    std::atomic<bool> stop_requested_{false};
    void* first_result_ = nullptr;  // std::promise<bool>* during start()
    bool started_ = false;
};

}  // namespace ir
