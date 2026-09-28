#pragma once

// Win32 foreground window/process tracking via SetWinEventHook (event-driven,
// no polling — spec §102, §121). Runs on a dedicated message-loop thread; on
// each foreground or title change it reads the foreground window's process name
// and title, routes the observation through an owned ContextTracker, pushes
// ContextChanged events, hands new/updated Context records to an optional sink
// (for storage), and publishes the active ContextId so other capture sources
// can stamp their events.
//
// Header avoids <Windows.h>; Windows types live in the .cpp. Single active
// instance per process (spec §412); bounded install retry/backoff (spec §384).

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "context/context_tracker.hpp"
#include "core/event_id_allocator.hpp"
#include "core/event_queue.hpp"
#include "core/ids.hpp"

namespace ir {

class WindowContextMonitor {
public:
    using ContextSink = std::function<void(const Context&)>;

    WindowContextMonitor(EventQueue& queue, EventIdAllocator& ids,
                         SessionId session, ContextSink on_context = {},
                         std::uint64_t first_context_id = 1);
    ~WindowContextMonitor();

    WindowContextMonitor(const WindowContextMonitor&) = delete;
    WindowContextMonitor& operator=(const WindowContextMonitor&) = delete;

    bool start();
    void stop();

    bool installed() const { return installed_.load(std::memory_order_acquire); }
    std::uint64_t changes() const {
        return changes_.load(std::memory_order_relaxed);
    }

    // Active context id (0 until the first foreground window is observed).
    ContextId current_context() const {
        return ContextId{current_context_.load(std::memory_order_acquire)};
    }

    // Called from the WinEvent callback on the monitor thread.
    void handle_foreground_change();

private:
    void run();
    bool install();
    void uninstall();
    bool retry_install();
    void observe_foreground();  // reads the OS foreground window (monitor thread)
    void push_diagnostic(const char* message);

    EventQueue& queue_;
    EventIdAllocator& ids_;
    SessionId session_;
    ContextSink on_context_;
    ContextTracker tracker_;  // touched only on the monitor thread

    std::thread thread_;
    std::atomic<unsigned long> thread_id_{0};
    std::atomic<bool> installed_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<std::uint64_t> changes_{0};
    std::atomic<std::uint64_t> current_context_{0};
    void* hook_handle_ = nullptr;   // HWINEVENTHOOK, owned by the monitor thread
    void* first_result_ = nullptr;  // std::promise<bool>* live only during start()
    bool started_ = false;
};

}  // namespace ir
