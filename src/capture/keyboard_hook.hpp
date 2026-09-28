#pragma once

// Global low-level keyboard capture via WH_KEYBOARD_LL (spec §57).
//
// The hook is installed on a dedicated capture thread that runs a message loop
// (required for low-level hooks). The hook callback does the minimum — build a
// RawKeyboardInput, translate, stamp id/session, enqueue, return — and never
// touches disk/UI/reconstruction (spec §58, §455). Only one instance may be
// active per process (spec §412). Installation uses a bounded retry/backoff and
// never spins (spec §383-§385).
//
// The header intentionally avoids <Windows.h>; Windows types live in the .cpp.

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

#include "core/event_id_allocator.hpp"
#include "core/event_queue.hpp"
#include "core/ids.hpp"
#include "keyboard/keyboard_text.hpp"
#include "keyboard/keyboard_translator.hpp"

namespace ir {

struct RawKeyboardInput;

class KeyboardHook {
public:
    KeyboardHook(EventQueue& queue, EventIdAllocator& ids, SessionId session);
    ~KeyboardHook();

    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;

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

    // Optional shared source of the active ContextId (published by the window
    // context monitor) so captured events are stamped with their context. The
    // pointed-to atomic must outlive this hook. nullptr leaves context at 0.
    void set_context_source(const std::atomic<std::uint64_t>* source) {
        context_source_ = source;
    }

    // Optional guard consulted per key: when it returns true (e.g. a password
    // field is focused), the key is not recorded at all — no keystroke and no
    // resolved text (spec: never capture credentials). Modifier/lock state is
    // still tracked so recording stays consistent afterwards.
    void set_sensitive_guard(std::function<bool()> guard) {
        sensitive_guard_ = std::move(guard);
    }

    // Called from the low-level hook callback (public so the file-scope proc can
    // reach it; not intended for external use).
    void on_raw_input(const RawKeyboardInput& raw);

private:
    void run();          // capture-thread body
    bool install();      // SetWindowsHookEx
    void uninstall();    // UnhookWindowsHookEx
    bool retry_install();
    void push_diagnostic(const char* message);

    EventQueue& queue_;
    EventIdAllocator& ids_;
    SessionId session_;
    KeyboardTranslator translator_;     // touched only on the capture thread
    KeyboardTextResolver text_resolver_;  // touched only on the capture thread
    std::function<bool()> sensitive_guard_;

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
