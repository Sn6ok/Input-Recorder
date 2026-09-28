#pragma once

// Global low-level mouse capture via WH_MOUSE_LL (spec §115).
//
// Mirrors KeyboardHook: installed on a dedicated capture thread running a
// message loop (required for low-level hooks). The callback does the minimum —
// build a RawMouseInput, process, stamp id/session, enqueue, return — and never
// touches disk/UI/reconstruction (spec §58, §455). Only one instance may be
// active per process (spec §412). Installation uses a bounded retry/backoff and
// never spins (spec §383-§385).
//
// The header intentionally avoids <Windows.h>; Windows types live in the .cpp.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

#include "core/event_id_allocator.hpp"
#include "core/event_queue.hpp"
#include "core/ids.hpp"
#include "mouse/mouse_processor.hpp"

namespace ir {

struct RawMouseInput;

class MouseHook {
public:
    MouseHook(EventQueue& queue, EventIdAllocator& ids, SessionId session,
              const MouseSettings& settings = {});
    ~MouseHook();

    MouseHook(const MouseHook&) = delete;
    MouseHook& operator=(const MouseHook&) = delete;

    // Starts capture. Returns true if the hook is installed by the time it
    // returns (first attempt). If the first attempt fails, returns false while a
    // bounded background retry continues; check installed() for live state.
    bool start();

    // Stops capture and joins the thread. Safe to call multiple times.
    void stop();

    bool installed() const { return installed_.load(std::memory_order_acquire); }
    std::uint64_t captured() const {
        return captured_.load(std::memory_order_relaxed);
    }

    // Thread-safe: update the recording toggles (e.g. when settings change).
    void set_settings(const MouseSettings& settings);

    // Optional shared source of the active ContextId (see KeyboardHook). The
    // pointed-to atomic must outlive this hook. nullptr leaves context at 0.
    void set_context_source(const std::atomic<std::uint64_t>* source) {
        context_source_ = source;
    }

    // Called from the low-level hook callback (public so the file-scope proc can
    // reach it; not intended for external use).
    void on_raw_input(const RawMouseInput& raw);

private:
    void run();          // capture-thread body
    bool install();      // SetWindowsHookEx
    void uninstall();    // UnhookWindowsHookEx
    bool retry_install();
    void push_diagnostic(const char* message);

    EventQueue& queue_;
    EventIdAllocator& ids_;
    SessionId session_;

    std::mutex proc_mutex_;          // guards processor_ (capture thread + setters)
    MouseProcessor processor_;

    std::thread thread_;
    std::atomic<unsigned long> thread_id_{0};
    std::atomic<bool> installed_{false};
    std::atomic<bool> stop_requested_{false};
    std::atomic<std::uint64_t> captured_{0};
    const std::atomic<std::uint64_t>* context_source_ = nullptr;
    void* hook_handle_ = nullptr;  // HHOOK, owned by the capture thread
    void* first_result_ = nullptr; // std::promise<bool>* live only during start()
    bool started_ = false;
};

}  // namespace ir
