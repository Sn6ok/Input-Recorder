#pragma once

// Asynchronous storage writer (spec §35, §170, §453). Capture threads submit
// records with non-blocking calls; a single dedicated worker thread batches
// everything currently staged into one SQLite transaction and commits it. This
// keeps disk I/O entirely off the capture path and amortises fsyncs across many
// events (spec §170). flush() gives tests and shutdown a deterministic barrier.
//
// OS-free and unit-tested against a temp/in-memory EventStore.

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include "core/clipboard_entry.hpp"
#include "core/context.hpp"
#include "core/event.hpp"
#include "core/text_snapshot.hpp"
#include "storage/event_store.hpp"

namespace ir {

class StorageWorker {
public:
    struct Stats {
        std::uint64_t events_written = 0;
        std::uint64_t contexts_written = 0;
        std::uint64_t clipboard_written = 0;
        std::uint64_t snapshots_written = 0;
        std::uint64_t transactions = 0;
        std::uint64_t write_errors = 0;
    };

    explicit StorageWorker(EventStore& store) : store_(store) {}
    ~StorageWorker();

    StorageWorker(const StorageWorker&) = delete;
    StorageWorker& operator=(const StorageWorker&) = delete;

    void start();
    void stop();  // flush remaining, then join

    // Non-blocking, thread-safe. Records are staged and written by the worker.
    void submit_event(Event e);
    void submit_context(const Context& c);
    void submit_clipboard_entry(const ClipboardEntry& e);
    void submit_snapshot(const TextSnapshot& s);

    // Blocks until everything submitted before this call has been written.
    void flush();

    Stats stats() const;
    std::size_t pending() const;

private:
    void run();
    bool has_pending_locked() const;

    EventStore& store_;

    mutable std::mutex mutex_;
    std::condition_variable work_cv_;     // worker waits for work/stop
    std::condition_variable flushed_cv_;  // flush() waiters
    std::thread thread_;
    bool started_ = false;
    bool stop_ = false;

    std::uint64_t submitted_ = 0;  // total items ever submitted
    std::uint64_t written_ = 0;    // total items the worker has processed

    std::vector<Event> events_;
    std::vector<Context> contexts_;
    std::vector<ClipboardEntry> clipboard_;
    std::vector<TextSnapshot> snapshots_;

    Stats stats_{};
};

}  // namespace ir
