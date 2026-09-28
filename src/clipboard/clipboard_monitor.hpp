#pragma once

// Win32 clipboard monitoring via a message-only window and
// AddClipboardFormatListener (event-driven; no polling, spec §102). Runs on a
// dedicated message-loop thread. On each change it reads CF_UNICODETEXT with a
// bounded retry (spec §113), converts to UTF-8, and routes it through an owned
// ClipboardProcessor, pushing ClipboardChanged events to the queue and handing
// new entries to an optional sink.
//
// Header avoids <Windows.h>; Windows types live in the .cpp.

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "clipboard/clipboard_processor.hpp"
#include "core/event_id_allocator.hpp"
#include "core/event_queue.hpp"
#include "core/ids.hpp"

namespace ir {

class ClipboardMonitor {
public:
    using EntrySink = std::function<void(const ClipboardEntry&)>;

    ClipboardMonitor(EventQueue& queue, EventIdAllocator& ids, SessionId session,
                     std::uint64_t max_bytes, EntrySink on_entry = {},
                     std::uint64_t first_entry_id = 1);
    ~ClipboardMonitor();

    ClipboardMonitor(const ClipboardMonitor&) = delete;
    ClipboardMonitor& operator=(const ClipboardMonitor&) = delete;

    bool start();
    void stop();

    bool installed() const { return installed_.load(std::memory_order_acquire); }
    std::uint64_t observed() const {
        return observed_.load(std::memory_order_relaxed);
    }

    // Announce that the app itself is about to set the clipboard (e.g. Copy All),
    // so the resulting change is not recorded as a user event (spec §497).
    void note_self_copy(const std::string& utf8);

    void set_current_context(ContextId context) {
        current_context_.store(context.value, std::memory_order_relaxed);
    }

    // Called from the window procedure on the monitor thread.
    void handle_clipboard_update();

private:
    void run();

    ClipboardProcessor processor_;
    std::mutex proc_mutex_;  // guards processor_ (monitor thread + note_self_copy)
    EventQueue& queue_;
    EntrySink on_entry_;

    std::thread thread_;
    std::atomic<unsigned long> thread_id_{0};
    std::atomic<bool> installed_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<std::uint64_t> observed_{0};
    std::atomic<std::uint64_t> current_context_{0};

    void* hwnd_ = nullptr;         // HWND, owned by the monitor thread
    void* first_result_ = nullptr; // std::promise<bool>* live only during start()
    bool started_ = false;
};

}  // namespace ir
