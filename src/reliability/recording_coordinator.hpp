#pragma once

// The pipeline coordinator (spec §39, §69, §170): a single consumer thread that
// drains the event queue and, for each event, (1) feeds the reconstruction
// engine, (2) persists it through the async StorageWorker, and (3) periodically
// captures a text snapshot per the SnapshotPolicy. It publishes a thread-safe
// view (current reconstructed text + confidence) for the UI.
//
// Graceful degradation (spec §463): when the app marks storage degraded (disk
// full / database unavailable), events are appended to the EmergencyBuffer
// instead, to be replayed once storage recovers — reconstruction keeps working
// throughout. OS-free and unit-tested by driving the queue directly.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/confidence.hpp"
#include "core/event_queue.hpp"
#include "core/ids.hpp"
#include "history/snapshot_policy.hpp"
#include "reconstruction/reconstruction_engine.hpp"
#include "reliability/emergency_buffer.hpp"
#include "storage/event_store.hpp"
#include "storage/storage_worker.hpp"

namespace ir {

class RecordingCoordinator {
public:
    struct Options {
        SnapshotSettings snapshots;
        std::size_t drain_batch = 256;
    };

    RecordingCoordinator(EventQueue& queue, StorageWorker& writer,
                         EventStore& store, SessionId session);
    RecordingCoordinator(EventQueue& queue, StorageWorker& writer,
                         EventStore& store, SessionId session, Options options,
                         EmergencyBuffer* emergency = nullptr);
    ~RecordingCoordinator();

    RecordingCoordinator(const RecordingCoordinator&) = delete;
    RecordingCoordinator& operator=(const RecordingCoordinator&) = delete;

    void start();
    void stop();  // closes the queue, drains the remainder, joins

    struct View {
        std::string text;
        Confidence confidence = Confidence::High;
        std::uint64_t processed = 0;
    };
    View view() const;
    std::uint64_t processed() const;
    std::uint64_t snapshots_taken() const {
        return snapshots_taken_.load(std::memory_order_relaxed);
    }

    // Route events to the emergency buffer instead of storage while true.
    void set_degraded(bool degraded) {
        degraded_.store(degraded, std::memory_order_release);
    }
    bool degraded() const { return degraded_.load(std::memory_order_acquire); }

private:
    void run();
    void process(const Event& e);
    void publish_view();

    EventQueue& queue_;
    StorageWorker& writer_;
    EventStore& store_;
    EmergencyBuffer* emergency_;
    SessionId session_;
    Options options_;

    // Consumer-thread-only state:
    ReconstructionEngine engine_;
    SnapshotPolicy snapshot_policy_;
    ContextId current_context_{};
    std::uint64_t next_snapshot_id_ = 1;
    std::uint64_t processed_count_ = 0;

    mutable std::mutex view_mutex_;
    std::string view_text_;
    Confidence view_confidence_ = Confidence::High;
    std::uint64_t view_processed_ = 0;

    std::atomic<bool> degraded_{false};
    std::atomic<std::uint64_t> snapshots_taken_{0};
    std::thread thread_;
    bool started_ = false;
};

}  // namespace ir
